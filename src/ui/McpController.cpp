#include "ui/McpController.h"
#include "ui/ProjectController.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QLockFile>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace lectern::ui {
namespace {
using mcp::Json;

void respond(QTcpSocket* socket, int code, const QByteArray& body = {}, const QByteArray& session = {}) {
    const QByteArray reason = code == 200 ? "OK" : code == 202 ? "Accepted" : code == 400 ? "Bad Request" :
        code == 401 ? "Unauthorized" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" :
        code == 405 ? "Method Not Allowed" : code == 413 ? "Content Too Large" : code == 429 ? "Too Many Requests" : "Error";
    QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\nConnection: close\r\n";
    head += "Content-Type: application/json\r\nCache-Control: no-store\r\n";
    if (!session.isEmpty()) head += "Mcp-Session-Id: " + session + "\r\n";
    if (code == 405) head += "Allow: POST, DELETE\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    socket->setProperty("answered", true);
    socket->write(head + body);
    socket->disconnectFromHost();
}

QByteArray randomToken() {
    QByteArray result;
    for (int i = 0; i < 8; ++i) result += QByteArray::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0');
    return result;
}

bool constantEqual(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}
} // namespace

McpController::McpController(ProjectController* project, QObject* parent, QString discoveryFile)
    : QObject(parent), project_(project), listener_(new QTcpServer(this)),
      discoveryFile_(discoveryFile.isEmpty() ? defaultDiscoveryFile() : std::move(discoveryFile)) {
    registerMcpTools(server_, project_);
    connect(listener_, &QTcpServer::newConnection, this, &McpController::acceptConnections);
    listener_->setMaxPendingConnections(16);
}

McpController::~McpController() { stop(); }

void McpController::enableAppTools(McpAppHooks hooks) { registerMcpAppTools(server_, std::move(hooks)); }

QString McpController::defaultDiscoveryFile() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Lectern/mcp/connection.json");
}

bool McpController::enabled() const { return listener_->isListening(); }

QString McpController::endpoint() const {
    return enabled() ? QStringLiteral("http://127.0.0.1:%1/mcp").arg(listener_->serverPort()) : QString();
}

QString McpController::bridgePath() const {
    const QDir bin(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    return bin.filePath(QStringLiteral("lectern-mcp.exe"));
#else
    const auto bundled = bin.filePath(QStringLiteral("lectern-mcp"));
    return QFileInfo::exists(bundled) ? bundled : QDir::cleanPath(bin.filePath(QStringLiteral("../../../lectern-mcp")));
#endif
}

void McpController::copySetup() {
    Json setup{{"mcpServers", {{"lectern", {{"command", bridgePath().toStdString()}, {"args", Json::array()}}}}}};
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(QString::fromStdString(setup.dump(2)));
}

QVariantList McpController::clients() const {
    QVariantList list;
    for (const auto& [id, s] : sessions_) {
        const auto level = s.access == mcp::Access::Read ? "read" : s.access == mcp::Access::Edit ? "edit" :
                           s.access == mcp::Access::Full ? "full" : "pending";
        list.append(QVariantMap{{"id", QString::fromStdString(id)}, {"name", QString::fromStdString(s.clientName)},
                                {"access", QString::fromLatin1(level)}});
    }
    return list;
}

void McpController::setEnabled(bool value) {
    if (value == enabled()) return;
    if (!value) { stop(); return; }
    const auto directory = QFileInfo(discoveryFile_).absolutePath();
    const auto fail = [this](const QString& message) { stop(); status_ = message; emit stateChanged(); };
    if (!QDir().mkpath(directory) || !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return fail(QStringLiteral("Could not create the private connection directory."));
    discoveryLock_ = std::make_unique<QLockFile>(discoveryFile_ + QStringLiteral(".lock"));
    if (!discoveryLock_->tryLock(0)) {
        discoveryLock_.reset();
        return fail(QStringLiteral("Assistant access is already enabled in another Lectern instance."));
    }
    if (!listener_->listen(QHostAddress::LocalHost, 0)) return fail(listener_->errorString());
    token_ = randomToken();
    QSaveFile file(discoveryFile_);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return fail(QStringLiteral("Could not save the private connection file."));
    const auto bytes = QByteArray::fromStdString(Json{{"url", endpoint().toStdString()}, {"token", token_.toStdString()}}.dump());
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(QStringLiteral("Could not save the connection."));
    status_ = QStringLiteral("Ready · approve each assistant below");
    emit stateChanged();
}

void McpController::stop() {
    listener_->close();
    for (auto* socket : listener_->findChildren<QTcpSocket*>()) socket->abort();
    sessions_.clear();
    token_.clear();
    if (discoveryLock_ && discoveryLock_->isLocked()) QFile::remove(discoveryFile_);
    discoveryLock_.reset();
    status_ = QStringLiteral("Assistant access is off");
    emit stateChanged();
    emit clientsChanged();
}

void McpController::approve(const QString& id, const QString& access) {
    const auto it = sessions_.find(id.toStdString());
    if (it == sessions_.end()) return;
    if (access != QLatin1String("read") && access != QLatin1String("edit")) return;
    it->second.access = access == QLatin1String("read") ? mcp::Access::Read : mcp::Access::Edit;
    log(QString::fromStdString(it->second.clientName), QStringLiteral("Access approved"), access);
    emit clientsChanged();
}

void McpController::revoke(const QString& id) {
    sessions_.erase(id.toStdString());
    emit clientsChanged();
}

int McpController::undoEdits(const QString& id) {
    return project_->undoAssistantEdits(id);
}

void McpController::log(const QString& client, const QString& action, const QString& outcome) {
    activity_.prepend(QVariantMap{{"client", client}, {"action", action}, {"outcome", outcome},
                                 {"time", QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))}});
    while (activity_.size() > 100) activity_.removeLast();
    emit activityChanged();
}

void McpController::acceptConnections() {
    while (auto* socket = listener_->nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        if (listener_->findChildren<QTcpSocket*>().size() > 32) { respond(socket, 429); continue; }
        socket->setReadBufferSize(static_cast<qint64>(mcp::Server::maxMessageBytes + 16384));
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { receive(socket); });
        QTimer::singleShot(10000, socket, [socket] { socket->abort(); });
    }
}

void McpController::receive(QTcpSocket* socket) {
    if (socket->property("answered").toBool()) return;
    auto buffer = socket->property("request").toByteArray();
    buffer += socket->readAll();
    if (buffer.size() > static_cast<qsizetype>(mcp::Server::maxMessageBytes + 16384)) { respond(socket, 413); return; }
    const auto end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (buffer.size() > 16384) respond(socket, 413);
        else socket->setProperty("request", buffer);
        return;
    }
    if (end > 16384) { respond(socket, 413); return; }
    const auto lines = buffer.left(end).split('\n');
    const auto request = lines.first().trimmed().split(' ');
    if (request.size() != 3 || request[2] != "HTTP/1.1") { respond(socket, 400); return; }
    QMap<QByteArray, QByteArray> headers;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed();
        const auto colon = line.indexOf(':');
        if (colon <= 0) { respond(socket, 400); return; }
        const auto name = line.left(colon).toLower();
        if (headers.contains(name)) { respond(socket, 400); return; }
        headers.insert(name, line.mid(colon + 1).trimmed());
    }
    const auto host = "127.0.0.1:" + QByteArray::number(listener_->serverPort());
    if (headers.value("host") != host || (headers.contains("origin") && headers["origin"] != "http://" + host)) {
        respond(socket, 403); return;
    }
    if (!constantEqual(headers.value("authorization"), "Bearer " + token_)) { respond(socket, 401); return; }
    if (request[1] != "/mcp") { respond(socket, 404); return; }
    if (request[0] != "POST" && request[0] != "DELETE") { respond(socket, 405); return; }
    if (headers.contains("transfer-encoding")) { respond(socket, 400); return; }
    auto it = sessions_.find(headers.value("mcp-session-id").toStdString());
    if (request[0] == "DELETE") {
        if (it == sessions_.end()) { respond(socket, 404); return; }
        sessions_.erase(it); emit clientsChanged(); respond(socket, 200, "{}"); return;
    }
    bool lengthOk = false;
    const auto length = headers.value("content-length").toLongLong(&lengthOk);
    if (!lengthOk || length < 0) { respond(socket, 400); return; }
    if (length > static_cast<qint64>(mcp::Server::maxMessageBytes)) { respond(socket, 413); return; }
    if (!headers.value("content-type").startsWith("application/json")) { respond(socket, 400); return; }
    if (!headers.value("accept").contains("application/json") || !headers.value("accept").contains("text/event-stream")) {
        respond(socket, 400); return;
    }
    if (buffer.size() < end + 4 + length) { socket->setProperty("request", buffer); return; }
    const auto body = buffer.mid(end + 4, length);
    Json message;
    try {
        message = Json::parse(body.constData(), body.constData() + body.size(), [](int depth, Json::parse_event_t, Json&) {
            if (depth > 64) throw std::invalid_argument("nesting limit");
            return true;
        });
    } catch (...) { respond(socket, 400, QByteArray::fromStdString(mcp::Server::error(nullptr, -32700, "Invalid JSON").dump())); return; }
    const bool initialize = message.is_object() && message.contains("method") && message["method"] == "initialize";
    if (initialize && it == sessions_.end()) {
        if (!headers.value("mcp-session-id").isEmpty()) { respond(socket, 404); return; }
        if (sessions_.size() >= 16) { respond(socket, 429); return; }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        it = sessions_.emplace(id, mcp::Session{.id = id}).first;
    } else if (it == sessions_.end()) { respond(socket, 404); return; }
    if (!initialize) {
        const auto version = headers.value("mcp-protocol-version", "2025-03-26");
        if (version.toStdString() != it->second.protocolVersion) { respond(socket, 400); return; }
    }
    const auto response = server_.handle(message, it->second);
    if (initialize) {
        if (!it->second.initialized) { sessions_.erase(it); respond(socket, 400, response ? QByteArray::fromStdString(response->dump()) : QByteArray()); return; }
        emit clientsChanged();
    }
    if (message.is_object() && message.value("method", Json()) == "tools/call") {
        const auto params = message.value("params", Json::object());
        const auto action = params.is_object() && params.contains("name") && params["name"].is_string() ? params["name"].get<std::string>() : "Invalid tool call";
        const bool failed = !response || response->contains("error") || response->value("result", Json::object()).value("isError", false);
        log(QString::fromStdString(it->second.clientName), QString::fromStdString(action), failed ? QStringLiteral("Refused / failed") : QStringLiteral("Completed"));
    }
    respond(socket, response ? 200 : 202, response ? QByteArray::fromStdString(response->dump()) : QByteArray(),
            initialize ? QByteArray::fromStdString(it->first) : QByteArray());
}

} // namespace lectern::ui
