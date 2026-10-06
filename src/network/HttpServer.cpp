#include "network/HttpServer.h"

#include "core/Log.h"

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = std::uintptr_t;
#define LECTERN_CLOSE_SOCKET closesocket
#define LECTERN_SOCKET_ERRNO WSAGetLastError()
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
#define LECTERN_CLOSE_SOCKET ::close
#define LECTERN_SOCKET_ERRNO errno
#endif

namespace lectern::net {
namespace {

constexpr std::string_view kCategory = "http";
/// Bounds a single request head so a client cannot make us buffer forever.
constexpr std::size_t kMaxHeadBytes = 16 * 1024;
constexpr int kAcceptPollMs = 200;
/// Idle limit for a keep-alive connection between requests.
constexpr int kSocketTimeoutSec = 5;
/// Idle limit for a WebSocket; the page sends a heartbeat every couple of
/// seconds even while its camera is paused.
constexpr int kWebSocketIdleSec = 15;

/// Strips the query string and decodes nothing else; the token is hex so no
/// percent-decoding is needed.
void splitPath(std::string_view target, std::string& path, std::string& query) {
    const auto mark = target.find('?');
    if (mark == std::string_view::npos) {
        path = std::string(target);
        query.clear();
    } else {
        path = std::string(target.substr(0, mark));
        query = std::string(target.substr(mark + 1));
    }
}

std::string lowered(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Case-insensitive search for a header value.
std::string_view headerValue(std::string_view head, std::string_view name) {
    const std::string haystack = lowered(head);
    const std::string needle = lowered(name);
    const auto at = haystack.find(needle + ":");
    if (at == std::string::npos) return {};
    auto value = head.substr(at + needle.size() + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    const auto end = value.find("\r\n");
    return end == std::string_view::npos ? value : value.substr(0, end);
}

}  // namespace

HttpServer::~HttpServer() { stop(); }

std::string HttpServer::lanAddress() {
#ifdef _WIN32
    char host[64]{};
    DWORD size = sizeof(host);
    if (GetAdaptersInfo(nullptr) == ERROR_BUFFER_OVERFLOW) {
        auto* info = static_cast<IP_ADAPTER_ADDRESSES*>(std::malloc(size));
        if (!info) return "127.0.0.1";
        GetAdaptersInfo(info, &size);
        for (auto* a = info; a; a = a->Next) {
            if (a->OperStatus == IfOperStatusUp && a->FirstUnicastAddress) {
                sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(a->FirstUnicastAddress->Address.lpSockaddr);
                if (sa->sin_family == AF_INET) {
                    inet_ntop(AF_INET, &sa->sin_addr, host, sizeof(host));
                    std::free(info);
                    return host;
                }
            }
        }
        std::free(info);
    }
    return "127.0.0.1";
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return "127.0.0.1";
    // en0 first (Wi-Fi on macOS), then any other up, non-loopback IPv4.
    std::string fallback = "127.0.0.1";
    for (const char* wanted : {"en0", "en1"}) {
        for (ifaddrs* it = list; it; it = it->ifa_next) {
            if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
            if (std::strcmp(it->ifa_name, wanted) != 0) continue;
            if (!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
            char host[INET_ADDRSTRLEN]{};
            const auto* sin = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
            if (inet_ntop(AF_INET, &sin->sin_addr, host, sizeof(host))) fallback = host;
            if (!fallback.empty() && fallback != "127.0.0.1") {
                ::freeifaddrs(list);
                return fallback;
            }
        }
    }
    for (ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        if (!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
        char host[INET_ADDRSTRLEN]{};
        const auto* sin = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, host, sizeof(host))) {
            fallback = host;
            break;
        }
    }
    ::freeifaddrs(list);
    return fallback;
#endif
}

Status HttpServer::start(std::uint16_t port, std::string_view token) {
    if (running()) return fail(ErrorCode::AlreadyExists, "http server already running");

    token_.assign(token);
    lanAddress_ = lanAddress();

    listenFd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (listenFd_ < 0) {
        return fail(errnoError(ErrorCode::NetworkError, "socket", LECTERN_SOCKET_ERRNO));
    }
    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int err = LECTERN_SOCKET_ERRNO;
        LECTERN_CLOSE_SOCKET(listenFd_);
        listenFd_ = -1;
        return fail(errnoError(ErrorCode::NetworkError, "bind", err));
    }
    if (::listen(listenFd_, 8) != 0) {
        const int err = LECTERN_SOCKET_ERRNO;
        LECTERN_CLOSE_SOCKET(listenFd_);
        listenFd_ = -1;
        return fail(errnoError(ErrorCode::NetworkError, "listen", err));
    }

    // Learn the port when 0 was requested.
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
        port_ = ntohs(bound.sin_port);
    } else {
        port_ = port;
    }

    if (auto tls = tls_.create(lanAddress_); !tls) {
        LECTERN_CLOSE_SOCKET(listenFd_);
        listenFd_ = -1;
        return fail(std::move(tls).error());
    }

    stopping_.store(false, std::memory_order_release);
    acceptThread_ = std::thread([this] { acceptLoop(); });
    LEC_INFO(kCategory, "listening on port {} for {}", port_, url());
    return ok();
}

void HttpServer::stop() {
    if (listenFd_ < 0) return;
    stopping_.store(true, std::memory_order_release);
    if (acceptThread_.joinable()) acceptThread_.join();
    LECTERN_CLOSE_SOCKET(listenFd_);
    listenFd_ = -1;
    // Keep-alive and WebSocket threads sit in a blocking read; shutting their
    // sockets down wakes them so they exit before `this` goes away.
    {
        std::lock_guard lock(connectionsMutex_);
        for (const int fd : connections_) ::shutdown(static_cast<Socket>(fd), SHUT_RDWR);
    }
    for (int i = 0; i < 250 && clients_.load(std::memory_order_acquire) > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (observer_) observer_->onClientCount(0);
}

void HttpServer::trackConnection(int fd, bool add) {
    std::lock_guard lock(connectionsMutex_);
    if (add) {
        connections_.push_back(fd);
    } else {
        std::erase(connections_, fd);
    }
}

std::string HttpServer::baseUrl() const {
    return "https://" + (lanAddress_.empty() ? std::string("127.0.0.1") : lanAddress_) + ":" + std::to_string(port_);
}

std::string HttpServer::url() const {
    return baseUrl() + "/" + token_;
}

void HttpServer::acceptLoop() {
    while (!stopping_.load(std::memory_order_acquire)) {
        pollfd pfd{.fd = static_cast<Socket>(listenFd_), .events = POLLIN, .revents = 0};
        const int ready = ::poll(&pfd, 1, kAcceptPollMs);
        if (ready <= 0) continue;

        const int fd = static_cast<int>(::accept(listenFd_, nullptr, nullptr));
        if (fd < 0) continue;
#ifdef SO_NOSIGPIPE
        // OpenSSL writes with plain write(): a phone that vanished mid-frame
        // must not kill the app with SIGPIPE.
        int noSigPipe = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
        if (clients_.load(std::memory_order_relaxed) >= kMaxConnections) {
            LECTERN_CLOSE_SOCKET(fd);
            continue;
        }
        clients_.fetch_add(1, std::memory_order_relaxed);
        if (observer_) observer_->onClientCount(clients_.load(std::memory_order_relaxed));
        trackConnection(fd, true);
        // Detached: stop() shuts the socket down to wake the thread, and the
        // atomic counter lets it wait for every thread to finish.
        std::thread([this, fd] {
            SSL* ssl = SSL_new(tls_.ctx());
            if (ssl) {
                SSL_set_fd(ssl, fd);
                if (SSL_accept(ssl) > 0) {
                    serve(fd, ssl);
                    SSL_shutdown(ssl);
                }
                SSL_free(ssl);
            }
            trackConnection(fd, false);
            LECTERN_CLOSE_SOCKET(fd);
            // Notify before the decrement: once the count reaches zero stop()
            // may return and the observer may be gone.
            const std::size_t remaining = clients_.load(std::memory_order_relaxed) - 1;
            if (observer_) observer_->onClientCount(remaining);
            clients_.fetch_sub(1, std::memory_order_acq_rel);
        }).detach();
    }
}

namespace {

ssize_t connRead(int fd, SSL* ssl, char* chunk, std::size_t size) {
    if (ssl) return SSL_read(ssl, chunk, static_cast<int>(size));
#ifdef _WIN32
    return ::recv(static_cast<Socket>(fd), chunk, static_cast<int>(size), 0);
#else
    return ::recv(fd, chunk, size, 0);
#endif
}

ssize_t connWrite(int fd, SSL* ssl, const char* data, std::size_t size) {
    if (ssl) return SSL_write(ssl, data, static_cast<int>(size));
#ifdef _WIN32
    return ::send(static_cast<Socket>(fd), data, static_cast<int>(size), 0);
#else
    return ::send(fd, data, size, MSG_NOSIGNAL);
#endif
}

bool writeAll(int fd, SSL* ssl, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = connWrite(fd, ssl, data + sent, size - sent);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void setTimeouts(int fd, int seconds) {
#ifdef _WIN32
    const DWORD ms = static_cast<DWORD>(seconds) * 1000;
    ::setsockopt(static_cast<Socket>(fd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    ::setsockopt(static_cast<Socket>(fd), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval timeout{.tv_sec = seconds, .tv_usec = 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

/// Sec-WebSocket-Accept for a client key (RFC 6455 §4.2.2).
std::string webSocketAccept(std::string_view key) {
    const std::string input = std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    unsigned char encoded[4 * ((SHA_DIGEST_LENGTH + 2) / 3) + 1]{};
    const int n = EVP_EncodeBlock(encoded, digest, SHA_DIGEST_LENGTH);
    return std::string(reinterpret_cast<const char*>(encoded), static_cast<std::size_t>(n));
}

}  // namespace

bool HttpServer::readRequest(int fd, SSL* ssl, std::string& carry, std::string& head,
                             std::vector<std::uint8_t>& body) {
    head.clear();
    body.clear();

    std::string buffer = std::move(carry);
    carry.clear();
    std::size_t headEnd = std::string_view::npos;
    std::size_t bodyLen = 0;

    while (true) {
        if (headEnd == std::string_view::npos) {
            headEnd = buffer.find("\r\n\r\n");
            if (headEnd != std::string_view::npos) {
                headEnd += 4;
                const auto value = headerValue(std::string_view(buffer).substr(0, headEnd), "content-length");
                if (!value.empty()) {
                    bodyLen = static_cast<std::size_t>(std::strtoull(std::string(value).c_str(), nullptr, 10));
                    if (bodyLen > kMaxBodyBytes) return false;
                }
            } else if (buffer.size() > kMaxHeadBytes) {
                return false;
            }
        }
        if (headEnd != std::string_view::npos && buffer.size() - headEnd >= bodyLen) break;

        char chunk[16384];
        const ssize_t got = connRead(fd, ssl, chunk, sizeof(chunk));
        if (got <= 0) return false;
        buffer.append(chunk, static_cast<std::size_t>(got));
    }

    head = buffer.substr(0, headEnd);
    const auto* payload = reinterpret_cast<const std::uint8_t*>(buffer.data()) + headEnd;
    body.assign(payload, payload + bodyLen);
    carry = buffer.substr(headEnd + bodyLen);
    return true;
}

void HttpServer::writeResponse(int fd, SSL* ssl, const HttpResponse& response, bool keepAlive) {
    std::string head = "HTTP/1.1 " + response.status + "\r\n";
    head += "Content-Type: " + response.contentType + "\r\n";
    head += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\n";
    head += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    for (const auto& extra : response.headers) head += extra + "\r\n";
    head += "\r\n";

    const std::string out = head + response.body;
    writeAll(fd, ssl, out.data(), out.size());
}

void HttpServer::serve(int fd, SSL* ssl) {
    setTimeouts(fd, kSocketTimeoutSec);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));

    std::string carry;
    std::string head;
    std::vector<std::uint8_t> body;
    // HTTP/1.1 keep-alive: the page's control posts reuse one TLS session
    // instead of handshaking per request.
    while (!stopping_.load(std::memory_order_acquire)) {
        if (!readRequest(fd, ssl, carry, head, body)) return;

        std::string path;
        std::string query;
        const auto firstSpace = head.find(' ');
        const auto secondSpace = firstSpace == std::string::npos ? std::string::npos : head.find(' ', firstSpace + 1);
        if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
            writeResponse(fd, ssl, HttpResponse::notFound(), false);
            return;
        }
        const std::string method = head.substr(0, firstSpace);
        splitPath(std::string_view(head).substr(firstSpace + 1, secondSpace - firstSpace - 1), path, query);
        const auto connection = lowered(headerValue(head, "connection"));
        const bool keepAlive = connection.find("close") == std::string::npos;

        if (method == "GET" && lowered(headerValue(head, "upgrade")) == "websocket") {
            const auto key = headerValue(head, "sec-websocket-key");
            if (!handler_ || key.empty() || !handler_->acceptWebSocket(path)) {
                writeResponse(fd, ssl, HttpResponse::notFound(), false);
                return;
            }
            const std::string reply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                      "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
                                      webSocketAccept(key) + "\r\n\r\n";
            if (!writeAll(fd, ssl, reply.data(), reply.size())) return;
            serveWebSocket(fd, ssl, path, carry);
            return;
        }

        HttpResponse response;
        if (!handler_) {
            response.status = "503 Service Unavailable";
        } else if (method == "GET") {
            response = handler_->onGet(path);
        } else if (method == "POST") {
            handler_->onPost(path, query, body);
            response.contentType = "application/json";
            response.body = "{\"ok\":true}";
        } else {
            response.status = "405 Method Not Allowed";
        }

        writeResponse(fd, ssl, response, keepAlive);
        if (!keepAlive) return;
    }
}

void HttpServer::serveWebSocket(int fd, SSL* ssl, const std::string& path, std::string& carry) {
    // A paused phone sends heartbeats, so a long silence means it is gone.
    setTimeouts(fd, kWebSocketIdleSec);

    std::size_t carryPos = 0;
    // Reads exactly `size` bytes, draining bytes left over from the upgrade
    // request first.
    const auto readExact = [&](std::uint8_t* out, std::size_t size) {
        std::size_t have = 0;
        const std::size_t fromCarry = std::min(size, carry.size() - carryPos);
        if (fromCarry > 0) {
            std::memcpy(out, carry.data() + carryPos, fromCarry);
            carryPos += fromCarry;
            have = fromCarry;
        }
        while (have < size) {
            const ssize_t got = connRead(fd, ssl, reinterpret_cast<char*>(out + have), size - have);
            if (got <= 0) return false;
            have += static_cast<std::size_t>(got);
        }
        return true;
    };
    const auto sendFrame = [&](std::uint8_t opcode, std::span<const std::uint8_t> payload) {
        // Server frames are unmasked; control payloads are always < 126 bytes.
        std::string frame;
        frame.push_back(static_cast<char>(0x80 | opcode));
        frame.push_back(static_cast<char>(std::min<std::size_t>(payload.size(), 125)));
        frame.append(reinterpret_cast<const char*>(payload.data()), std::min<std::size_t>(payload.size(), 125));
        return writeAll(fd, ssl, frame.data(), frame.size());
    };

    std::vector<std::uint8_t> message;
    bool messageBinary = false;
    std::vector<std::uint8_t> payload;
    while (!stopping_.load(std::memory_order_acquire)) {
        std::uint8_t header[2];
        if (!readExact(header, 2)) return;
        const bool fin = (header[0] & 0x80) != 0;
        const std::uint8_t opcode = header[0] & 0x0F;
        const bool masked = (header[1] & 0x80) != 0;
        std::uint64_t length = header[1] & 0x7F;
        if (length == 126) {
            std::uint8_t ext[2];
            if (!readExact(ext, 2)) return;
            length = (std::uint64_t{ext[0]} << 8) | ext[1];
        } else if (length == 127) {
            std::uint8_t ext[8];
            if (!readExact(ext, 8)) return;
            length = 0;
            for (const std::uint8_t b : ext) length = (length << 8) | b;
        }
        // RFC 6455 §5.1: a client must mask every frame.
        if (!masked || length > kMaxBodyBytes || message.size() + length > kMaxBodyBytes) return;
        std::uint8_t mask[4];
        if (!readExact(mask, 4)) return;
        payload.resize(static_cast<std::size_t>(length));
        if (length > 0 && !readExact(payload.data(), payload.size())) return;
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i & 3];

        switch (opcode) {
            case 0x0:  // continuation
                message.insert(message.end(), payload.begin(), payload.end());
                break;
            case 0x1:  // text
            case 0x2:  // binary
                message.assign(payload.begin(), payload.end());
                messageBinary = opcode == 0x2;
                break;
            case 0x8:  // close: echo it and hang up
                sendFrame(0x8, std::span(payload).first(std::min<std::size_t>(payload.size(), 2)));
                return;
            case 0x9:  // ping
                if (!sendFrame(0xA, payload)) return;
                continue;
            case 0xA:  // pong
                continue;
            default: return;
        }
        if (fin) {
            if (handler_) handler_->onWebSocketMessage(path, message, messageBinary);
            message.clear();
        }
        if (carryPos >= carry.size()) {
            carry.clear();
            carryPos = 0;
        }
    }
}

}  // namespace lectern::net
