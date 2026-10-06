// lectern-mcp — the stdio MCP server that AI assistants (Claude, Codex, …) launch.
//
// App mode (default): relays every message to the running Lectern app's local
// endpoint, found through the connection file that Assistants → Allow AI
// Assistants writes; Lectern asks the user to approve each assistant. The
// bridge may start before Lectern does (it answers the handshake and the tool
// list itself), opens a new session when Lectern restarts or ends the old one,
// and ends its session when the assistant goes away.
//
// Headless (--project DIR): serves one project directly, read-only unless
// --allow-edits; every edit is on disk before its reply is sent.
//
// stdin is read on its own thread; messages are handled in order on the main
// thread, whose event loop keeps running (saves, network, signals).

#include "core/Log.h"
#include "media/FFmpeg.h"
#include "mcp/Server.h"
#include "ui/McpController.h"
#include "ui/ProjectController.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

#ifndef Q_OS_WIN
#include <QSocketNotifier>
#include <csignal>
#include <unistd.h>
#endif

using namespace lectern;
using mcp::Json;

namespace {

void output(const Json& message) { std::cout << message.dump() << '\n' << std::flush; }

constexpr const char* kNotReachable =
    "Lectern isn't reachable. Open Lectern, turn on Assistants → Allow AI Assistants, then approve this assistant "
    "when Lectern asks.";

/// One message from stdin, or the end of input.
struct Item {
    enum Kind { Line, TooLarge, End } kind = End;
    std::string text;
};

/// Reads stdin on its own thread and hands the lines to `handle` on the main
/// thread, one at a time and in order (also while a reply is awaited in a
/// nested event loop).
class Input {
public:
    Input(QObject* context, std::function<void(const Item&)> handle) : context_(context), handle_(std::move(handle)) {}
    ~Input() {
        if (reader_.joinable()) reader_.detach();  // still blocked on stdin: the process is ending anyway
    }

    void start() {
        reader_ = std::thread([this] {
            std::string line;
            bool oversized = false;
            char ch;
            while (std::cin.get(ch)) {
                if (ch != '\n') {
                    if (line.size() < mcp::Server::maxMessageBytes) line += ch;
                    else oversized = true;
                    continue;
                }
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (oversized) push({Item::TooLarge, {}});
                else if (!line.empty()) push({Item::Line, std::move(line)});
                line.clear();
                oversized = false;
            }
            push({Item::End, {}});
        });
    }
    /// After the end of input was handled: the reader has nothing left to do.
    void join() {
        if (reader_.joinable()) reader_.join();
    }

private:
    void push(Item item) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(item));
        }
        QMetaObject::invokeMethod(context_, [this] { drain(); }, Qt::QueuedConnection);
    }
    void drain() {
        if (busy_) return;  // a reply is being awaited: the next message waits its turn
        busy_ = true;
        for (;;) {
            Item item;
            {
                std::lock_guard lock(mutex_);
                if (queue_.empty()) break;
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            handle_(item);
        }
        busy_ = false;
    }

    QObject* context_;
    std::function<void(const Item&)> handle_;
    std::thread reader_;
    std::mutex mutex_;
    std::deque<Item> queue_;
    bool busy_ = false;
};

bool isLecternRequest(const std::string& method) {
    return method == "tools/list" || method == "tools/call" || method == "resources/list" || method == "resources/read" ||
           method == "resources/templates/list" || method == "prompts/list" || method == "prompts/get";
}

/// App mode: one assistant's session with the running app.
class Bridge {
public:
    explicit Bridge(QString connectionFile) : file_(std::move(connectionFile)) {
        // The same tool, resource and prompt lists as the app, for when it is not running.
        ui::registerMcpTools(local_, &definitions_);
        ui::registerMcpAppTools(local_, {[] { return QVariantList(); }, [](const QString&) {}});
    }

    void handle(const Json& m) {
        if (!m.is_object() || !m.contains("method") || !m["method"].is_string()) {  // malformed: the standard errors
            if (auto reply = local_.handle(m, localSession_)) output(*reply);
            return;
        }
        const std::string method = m["method"];
        if (method == "initialize") {
            const bool repeated = !hello_.is_null();
            auto local = local_.handle(m, localSession_);  // validates and negotiates the same way the app does
            if (repeated || !local || local->contains("error")) {
                if (local) output(repeated ? mcp::Server::error(m.value("id", Json()), -32600, "Session already initialized") : *local);
                return;
            }
            hello_ = m;
            if (open()) output(helloReply_);  // Lectern is running: its own answer (and approval request)
            else output(*local);
            return;
        }
        if (!m.contains("id")) {  // notifications
            if (method == "notifications/initialized") {
                ready_ = true;
                (void)local_.handle(m, localSession_);
            }
            if (!session_.isEmpty()) (void)post(m);
            return;
        }
        if (method == "ping" || hello_.is_null() || !isLecternRequest(method)) {  // answered here
            if (auto reply = local_.handle(m, localSession_)) output(*reply);
            return;
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (session_.isEmpty() && !open()) break;
            const Reply reply = post(m);
            if (reply.outcome == Outcome::Answered) {
                output(reply.body ? *reply.body : mcp::Server::error(m["id"], -32603, "Lectern sent no reply"));
                return;
            }
            if (reply.outcome == Outcome::Refused || reply.outcome == Outcome::Unknown) {
                output(mcp::Server::error(m["id"], -32000, reply.detail.toStdString()));
                return;
            }
            // Ended (the user ended this session) or nothing listening (Lectern restarted
            // on a new port, or quit): the request did not run. Start a new session
            // once from the current connection file; it needs approval again.
            session_.clear();
        }
        unreachable(m);
    }

    /// Ends the session in Lectern (the assistant went away).
    void close() {
        if (session_.isEmpty()) return;
        (void)send("DELETE", {}, 2000);
        session_.clear();
    }

private:
    enum class Outcome {
        Answered,     ///< 200 / 202
        Ended,        ///< 401 / 404: the session or token is gone; nothing ran
        Refused,      ///< 400 / 403 / 413 / 429: nothing ran; retrying won't help
        Unreachable,  ///< no connection file, or nothing listening
        Unknown       ///< timeout or a broken reply: it may have run
    };
    struct Reply {
        Outcome outcome = Outcome::Unknown;
        std::optional<Json> body;
        QString detail;
    };

    bool loadConnection() {
        QFile file(file_);
        const QFileInfo info(file);
        if (!info.isFile()) return false;  // Lectern is closed or assistants are off
#ifndef Q_OS_WIN
        const auto unsafe = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther;
        if (info.permissions() & unsafe) return warn("The connection file must be readable only by its owner.");
#endif
        if (info.isSymLink() || info.size() > 4096 || !file.open(QIODevice::ReadOnly)) return warn("Cannot read the connection file.");
        try {
            const auto config = Json::parse(file.readAll().toStdString());
            url_ = QUrl(QString::fromStdString(config.at("url").get<std::string>()));
            token_ = QByteArray::fromStdString(config.at("token").get<std::string>());
        } catch (...) {
            return warn("Invalid connection file.");
        }
        if (url_.scheme() != QLatin1String("http") || url_.host() != QLatin1String("127.0.0.1") || url_.port() <= 0 ||
            url_.path() != QLatin1String("/mcp") || !url_.userInfo().isEmpty() || url_.hasQuery() || url_.hasFragment() ||
            token_.size() != 64) {
            return warn("The connection file must identify Lectern's local endpoint.");
        }
        return true;
    }

    bool warn(const char* message) {
        if (warned_ != message) std::cerr << message << '\n';
        warned_ = message;
        return false;
    }

    /// (Re)starts the session by replaying the assistant's handshake.
    bool open() {
        session_.clear();
        if (hello_.is_null() || !loadConnection()) return false;
        const Reply reply = post(hello_);
        if (reply.outcome != Outcome::Answered || !reply.body || !reply.body->contains("result") || session_.isEmpty()) {
            if (reply.outcome == Outcome::Refused) refusal_ = reply.detail;
            session_.clear();
            return false;
        }
        refusal_.clear();
        helloReply_ = *reply.body;
        version_ = QByteArray::fromStdString((*reply.body)["result"].value("protocolVersion", std::string(mcp::Server::protocolVersion)));
        if (ready_) (void)post(Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
        return true;
    }

    /// Lectern is not there: lists come from the bridge, calls explain what to do.
    void unreachable(const Json& m) {
        const std::string method = m["method"];
        const std::string why = refusal_.isEmpty() ? std::string(kNotReachable) : refusal_.toStdString();
        if (method == "tools/call") output(Json{{"jsonrpc", "2.0"}, {"id", m["id"]}, {"result", mcp::Server::failure(why)}});
        else if (method == "resources/read") output(mcp::Server::error(m["id"], -32000, why));
        else if (auto reply = local_.handle(m, localSession_)) output(*reply);
    }

    Reply post(const Json& message) { return send("POST", QByteArray::fromStdString(message.dump()), 30000); }

    Reply send(const QByteArray& verb, const QByteArray& body, int timeoutMs) {
        QNetworkRequest request(url_);
        request.setRawHeader("Authorization", "Bearer " + token_);
        request.setRawHeader("Accept", "application/json, text/event-stream");
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        if (!session_.isEmpty()) {
            request.setRawHeader("Mcp-Session-Id", session_);
            request.setRawHeader("MCP-Protocol-Version", version_);
        }
        QNetworkReply* reply = verb == "DELETE" ? network_.deleteResource(request) : network_.post(request, body);
        QEventLoop wait;
        QTimer timeout;
        timeout.setSingleShot(true);
        bool timedOut = false;
        QObject::connect(&timeout, &QTimer::timeout, reply, [&timedOut, reply] { timedOut = true; reply->abort(); });
        QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
        timeout.start(timeoutMs);
        if (!reply->isFinished()) wait.exec();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray bytes = reply->readAll();
        const QByteArray id = reply->rawHeader("Mcp-Session-Id");
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (!id.isEmpty()) session_ = id;

        Reply out;
        if (timedOut) {
            out.detail = QStringLiteral("Lectern did not answer in time. The request may have been applied: check before retrying.");
            return out;
        }
        if (status == 0) {  // no HTTP answer: refused means nothing was delivered; anything else may have run
            const bool gone = error == QNetworkReply::ConnectionRefusedError || error == QNetworkReply::HostNotFoundError;
            out.outcome = gone ? Outcome::Unreachable : Outcome::Unknown;
            out.detail = QStringLiteral("Lectern connection failed (%1). The request may have been applied: check before retrying.")
                             .arg(errorText);
            return out;
        }
        std::optional<Json> json;
        if (!bytes.isEmpty()) {
            try {
                json = Json::parse(bytes.toStdString());
            } catch (...) {
            }
        }
        if (status == 200 || status == 202) {
            if (status == 200 && !json) {
                out.detail = QStringLiteral("Invalid reply from Lectern.");
                return out;
            }
            out.outcome = Outcome::Answered;
            out.body = json;
            return out;
        }
        if (status == 401 || status == 404) {
            out.outcome = Outcome::Ended;
            return out;
        }
        out.outcome = Outcome::Refused;
        if (json && json->contains("error") && (*json)["error"].contains("message")) {
            out.detail = QString::fromStdString((*json)["error"]["message"].get<std::string>());
        } else if (status == 429) {
            out.detail = QStringLiteral("Lectern has too many assistant connections. End unused ones in Assistants → Manage Assistants.");
        } else {
            out.detail = QStringLiteral("Lectern refused the request (HTTP %1).").arg(status);
        }
        return out;
    }

    QString file_;
    QNetworkAccessManager network_;
    QUrl url_;
    QByteArray token_;
    QByteArray session_;
    QByteArray version_ = QByteArray::fromStdString(std::string(mcp::Server::protocolVersion));
    Json hello_;       ///< the assistant's initialize request, replayed for each new session
    Json helloReply_;  ///< Lectern's answer to it
    bool ready_ = false;  ///< the assistant sent notifications/initialized
    QString refusal_;     ///< why Lectern refused the session (e.g. the user denied this assistant)
    std::string warned_;
    ui::ProjectController definitions_;  ///< never opened: only for the tool definitions
    mcp::Server local_;
    mcp::Session localSession_{.id = "local"};
};

#ifndef Q_OS_WIN
int signalPipe[2] = {-1, -1};
void onSignal(int) {
    const char byte = 1;
    (void)!::write(signalPipe[1], &byte, 1);
}
#endif

}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);  // buffered stdin; replies are flushed one by one
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QString projectPath;
    QString connection = ui::McpController::defaultDiscoveryFile();
    bool edit = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if ((arg == QLatin1String("--project") || arg == QLatin1String("--connection-file")) && i + 1 < argc) {
            const QString value = QString::fromLocal8Bit(argv[++i]);
            if (arg == QLatin1String("--project")) projectPath = value; else connection = value;
        } else if (arg == QLatin1String("--allow-edits")) edit = true;
        else if (arg == QLatin1String("--help")) {
            std::cout << "lectern-mcp [--connection-file FILE]\nlectern-mcp --project DIR [--allow-edits]\n"
                         "Default: relay to the running Lectern app (Assistants → Allow AI Assistants); Lectern asks you to\n"
                         "approve each assistant. Lectern may start later; the bridge reconnects when it restarts.\n"
                         "Headless: read-only unless --allow-edits is explicitly supplied. Close the project in the app before headless editing.\n";
            return 0;
        } else { std::cerr << "Unknown or incomplete argument. Use --help.\n"; return 2; }
    }
    if (edit && projectPath.isEmpty()) { std::cerr << "--allow-edits requires --project. App access is approved in Lectern.\n"; return 2; }
    Logger::instance().addSink(makeStderrSink());
    Logger::instance().setLevel(LogLevel::Warn);
    media::initializeFFmpeg(LogLevel::Error);
#ifndef Q_OS_WIN
    std::signal(SIGPIPE, SIG_IGN);  // the assistant went away: finish quietly
#endif

    std::unique_ptr<ui::ProjectController> project;
    std::unique_ptr<mcp::Server> server;
    mcp::Session session{.id = "headless", .access = edit ? mcp::Access::Edit : mcp::Access::Read};
    std::unique_ptr<Bridge> bridge;
    if (!projectPath.isEmpty()) {
        if (edit && QFileInfo::exists(connection)) { std::cerr << "Disable the app's assistant endpoint and close its project before headless editing.\n"; return 2; }
        project = std::make_unique<ui::ProjectController>();
        QEventLoop wait;
        QObject::connect(project.get(), &ui::ProjectController::opened, &wait, &QEventLoop::quit);
        QObject::connect(project.get(), &ui::ProjectController::failed, &wait, [&wait](const QString& error) {
            std::cerr << error.toStdString() << '\n';
            wait.quit();
        });
        project->open(projectPath);
        wait.exec();
        if (!project->loaded()) return 1;
        server = std::make_unique<mcp::Server>();
        ui::registerMcpTools(*server, project.get());
    } else {
        bridge = std::make_unique<Bridge>(connection);
    }

    // Leave cleanly: edits are already on disk; the app session is ended.
    const auto finish = [&] {
        if (bridge) bridge->close();
        if (project && project->dirty()) project->flushSaves();
    };
    Input input(&app, [&](const Item& item) {
        if (item.kind == Item::End) {
            finish();
            QCoreApplication::exit(0);
        } else if (item.kind == Item::TooLarge) {
            output(mcp::Server::error(nullptr, -32600, "Message too large"));
        } else if (bridge) {
            Json message;
            try {
                message = Json::parse(item.text, [](int depth, Json::parse_event_t, Json&) {
                    if (depth > 64) throw std::invalid_argument("nesting limit");
                    return true;
                });
            } catch (...) {
                output(mcp::Server::error(nullptr, -32700, "Invalid JSON"));
                return;
            }
            bridge->handle(message);
        } else {
            auto reply = server->handle(std::string_view(item.text), session, output);
            if (project->dirty()) project->flushSaves();  // on disk before the assistant hears "done"
            if (reply) output(*reply);
        }
    });

#ifndef Q_OS_WIN
    // Terminated by the assistant: end the session too (the reader may be blocked on stdin).
    std::unique_ptr<QSocketNotifier> stopRequest;
    if (::pipe(signalPipe) == 0) {
        struct sigaction action {};
        action.sa_handler = onSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        for (int sig : {SIGTERM, SIGINT, SIGHUP}) sigaction(sig, &action, nullptr);
        stopRequest = std::make_unique<QSocketNotifier>(signalPipe[0], QSocketNotifier::Read);
        QObject::connect(stopRequest.get(), &QSocketNotifier::activated, &app, [&] {
            finish();
            std::cout.flush();
            std::_Exit(0);
        });
    }
#endif

    input.start();
    const int code = QCoreApplication::exec();
    input.join();
    return code;
}
