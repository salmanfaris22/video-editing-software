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

#include <iostream>

using namespace lectern;
using mcp::Json;

namespace {
void output(const Json& message) { std::cout << message.dump() << '\n' << std::flush; }

struct Bridge {
    QNetworkAccessManager network;
    QUrl url;
    QByteArray token;
    QByteArray session;
    QByteArray version = QByteArray::fromStdString(std::string(mcp::Server::protocolVersion));

    bool load(const QString& fileName) {
        QFile file(fileName);
        const QFileInfo info(file);
#ifndef Q_OS_WIN
        const auto unsafe = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther;
        if (info.permissions() & unsafe) { std::cerr << "Connection file must be readable only by its owner.\n"; return false; }
#endif
        if (!info.isFile() || info.isSymLink() || info.size() > 4096 || !file.open(QIODevice::ReadOnly)) {
            std::cerr << "Open Lectern and enable AI assistants first.\n"; return false;
        }
        try {
            const auto config = Json::parse(file.readAll().toStdString());
            url = QUrl(QString::fromStdString(config.at("url").get<std::string>()));
            token = QByteArray::fromStdString(config.at("token").get<std::string>());
        } catch (...) { std::cerr << "Invalid connection file.\n"; return false; }
        if (url.scheme() != QLatin1String("http") || url.host() != QLatin1String("127.0.0.1") || url.port() <= 0 ||
            url.path() != QLatin1String("/mcp") || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || token.size() != 64) {
            std::cerr << "The connection file must identify Lectern's local endpoint.\n"; return false;
        }
        return true;
    }

    bool send(const std::string& line) {
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + token);
        request.setRawHeader("Accept", "application/json, text/event-stream");
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        if (!session.isEmpty()) {
            request.setRawHeader("Mcp-Session-Id", session);
            request.setRawHeader("MCP-Protocol-Version", version);
        }
        auto* reply = network.post(request, QByteArray::fromStdString(line));
        QEventLoop wait;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
        QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
        timeout.start(30000);
        wait.exec();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes = reply->readAll();
        const auto id = reply->rawHeader("Mcp-Session-Id");
        if (!id.isEmpty()) session = id;
        reply->deleteLater();
        if (status == 202) return true;
        if (status == 200) {
            try {
                const auto message = Json::parse(bytes.toStdString());
                if (message.contains("result") && message["result"].contains("protocolVersion"))
                    version = QByteArray::fromStdString(message["result"]["protocolVersion"].get<std::string>());
                output(message);
                return true;
            } catch (...) { std::cerr << "Invalid reply from Lectern.\n"; return false; }
        }
        // Transport errors do not fabricate tool success or retry a mutation.
        try {
            const auto message = Json::parse(line);
            if (message.is_object() && message.contains("id"))
                output(mcp::Server::error(message["id"], -32000, "Lectern connection failed (HTTP " + std::to_string(status) + "). Reconnect and approve this client in Lectern."));
        } catch (...) { output(mcp::Server::error(nullptr, -32700, "Invalid JSON")); }
        std::cerr << "Lectern connection failed (HTTP " << status << ").\n";
        return false;
    }
};
} // namespace

int main(int argc, char** argv) {
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
                         "Default: connect to the app; approve the client in AI assistants.\n"
                         "Headless: read-only unless --allow-edits is explicitly supplied. Close the project in the app before headless editing.\n";
            return 0;
        } else { std::cerr << "Unknown or incomplete argument. Use --help.\n"; return 2; }
    }
    if (edit && projectPath.isEmpty()) { std::cerr << "--allow-edits requires --project. App access is approved in Lectern.\n"; return 2; }
    Logger::instance().addSink(makeStderrSink());
    Logger::instance().setLevel(LogLevel::Warn);
    media::initializeFFmpeg(LogLevel::Error);
    ui::ProjectController project;
    mcp::Server server;
    mcp::Session session{.id = "headless", .access = edit ? mcp::Access::Edit : mcp::Access::Read};
    Bridge bridge;
    if (!projectPath.isEmpty()) {
        if (edit && QFileInfo::exists(connection)) { std::cerr << "Disable the app's assistant endpoint and close its project before headless editing.\n"; return 2; }
        QEventLoop wait;
        QObject::connect(&project, &ui::ProjectController::opened, &wait, &QEventLoop::quit);
        QObject::connect(&project, &ui::ProjectController::failed, &wait, [&wait](const QString& error) {
            std::cerr << error.toStdString() << '\n'; wait.quit();
        });
        project.open(projectPath);
        wait.exec();
        if (!project.loaded()) return 1;
        ui::registerMcpTools(server, &project);
    } else if (!bridge.load(connection)) return 1;

    std::string line;
    bool oversized = false;
    char ch;
    while (std::cin.get(ch)) {
        if (ch != '\n') {
            if (line.size() < mcp::Server::maxMessageBytes) line += ch;
            else oversized = true;
            continue;
        }
        QCoreApplication::processEvents();
        if (oversized) output(mcp::Server::error(nullptr, -32600, "Message too large"));
        else if (!line.empty()) {
            if (projectPath.isEmpty()) { if (!bridge.send(line)) return 1; }
            else if (auto reply = server.handle(std::string_view(line), session, output)) output(*reply);
        }
        line.clear(); oversized = false;
    }
    return 0;
}
