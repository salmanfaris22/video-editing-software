#pragma once

#include "core/Error.h"
#include "network/TlsServerContext.h"

#include <openssl/ssl.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace lectern::net {

/// A response the server writes back for a GET.
struct HttpResponse {
    std::string status = "200 OK";
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;
    /// Extra response headers, one "Name: value" per entry.
    std::vector<std::string> headers;

    static HttpResponse html(std::string page) {
        HttpResponse r;
        r.contentType = "text/html; charset=utf-8";
        r.body = std::move(page);
        return r;
    }
    static HttpResponse json(std::string payload) {
        HttpResponse r;
        r.contentType = "application/json";
        r.body = std::move(payload);
        return r;
    }
    static HttpResponse notFound() {
        HttpResponse r;
        r.status = "404 Not Found";
        r.body = "not found";
        return r;
    }
};

/// Requests the server dispatches. Every method may be called from a worker
/// thread, never concurrently for the same connection.
class IHttpHandler {
public:
    virtual ~IHttpHandler() = default;
    [[nodiscard]] virtual HttpResponse onGet(std::string_view path) = 0;
    /// `body` is the raw request body (already read to Content-Length).
    virtual void onPost(std::string_view path, std::string_view query, std::span<const std::uint8_t> body) = 0;

    /// Return true to accept a WebSocket upgrade on `path`. A WebSocket keeps
    /// one connection open for a whole stream, so frames pay no per-request
    /// TLS handshake or round trip.
    [[nodiscard]] virtual bool acceptWebSocket(std::string_view /*path*/) { return false; }
    /// One complete (reassembled) WebSocket message from an accepted socket.
    virtual void onWebSocketMessage(std::string_view /*path*/, std::span<const std::uint8_t> /*data*/,
                                    bool /*binary*/) {}
};

/// Notified when a client opens or closes a connection, so the UI can show
/// "waiting for phone" / "connected".
class IHttpObserver {
public:
    virtual ~IHttpObserver() = default;
    virtual void onClientCount(std::size_t connected) = 0;
};

/// Single-purpose HTTP/1.1 server for the phone camera page.
///
/// Deliberately tiny and LAN-only: it serves one page, accepts frame uploads
/// and control posts over HTTPS with an ephemeral self-signed certificate so
/// phone browsers allow camera access. The pairing token in the URL plus the
/// rule that the server only listens while the pair sheet is open provide
/// access control (docs/PHONE_CAMERA_PROTOCOL.md §4, §12).
///
/// Every connection gets its own thread, capped at kMaxConnections. Plain
/// requests use HTTP/1.1 keep-alive; the frame stream upgrades to a WebSocket
/// (RFC 6455) so a 30 fps camera costs one connection, not 30 handshakes a
/// second.
class HttpServer {
public:
    static constexpr std::size_t kMaxConnections = 16;
    /// Refuses bodies larger than this so a stray client cannot exhaust memory.
    static constexpr std::size_t kMaxBodyBytes = 8u * 1024 * 1024;

    HttpServer() = default;
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// Binds `port` on all interfaces and starts accepting. Pass 0 for an
    /// ephemeral port.
    Status start(std::uint16_t port, std::string_view token);
    void stop();
    [[nodiscard]] bool running() const noexcept { return listenFd_ >= 0; }
    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
    [[nodiscard]] std::size_t clientCount() const noexcept { return clients_.load(std::memory_order_relaxed); }

    /// Link to hand to the phone: `https://<lan-ip>:<port>/<token>`.
    [[nodiscard]] std::string url() const;
    /// Same host without the token, for display when no LAN route exists.
    [[nodiscard]] std::string baseUrl() const;

    void setHandler(IHttpHandler* handler) noexcept { handler_ = handler; }
    void setObserver(IHttpObserver* observer) noexcept { observer_ = observer; }

    /// This machine's LAN IPv4 address, or "127.0.0.1" when offline.
    [[nodiscard]] static std::string lanAddress();

private:
    void acceptLoop();
    void serve(int fd, SSL* ssl);
    /// Reads one request; returns false when the peer closed or misbehaved.
    /// `carry` holds bytes read past the previous request on this connection.
    bool readRequest(int fd, SSL* ssl, std::string& carry, std::string& head, std::vector<std::uint8_t>& body);
    void writeResponse(int fd, SSL* ssl, const HttpResponse& response, bool keepAlive);
    /// Runs an upgraded connection until the peer closes or the server stops.
    void serveWebSocket(int fd, SSL* ssl, const std::string& path, std::string& carry);
    void trackConnection(int fd, bool add);

    TlsServerContext tls_;

    int listenFd_ = -1;
    std::uint16_t port_ = 0;
    std::string token_;
    std::string lanAddress_;
    IHttpHandler* handler_ = nullptr;
    IHttpObserver* observer_ = nullptr;
    std::atomic<bool> stopping_{false};
    std::atomic<std::size_t> clients_{0};
    std::thread acceptThread_;
    /// Open connection sockets, so stop() can wake threads blocked in a read.
    std::mutex connectionsMutex_;
    std::vector<int> connections_;
};

}  // namespace lectern::net