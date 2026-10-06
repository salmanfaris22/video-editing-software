#pragma once

#include "mcp/Server.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <set>

class QTcpServer;
class QTcpSocket;
class QLockFile;

namespace lectern::ui {
class ProjectController;

/// What the app (not the headless bridge) adds: the recent projects and opening one.
struct McpAppHooks {
    std::function<QVariantList()> recentProjects;
    std::function<void(const QString& dir)> openProject;
};

/// All socket callbacks and command dispatch run on the UI thread.
class McpController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppController")
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY stateChanged)
    Q_PROPERTY(QString bridgePath READ bridgePath CONSTANT)
    Q_PROPERTY(QVariantList clients READ clients NOTIFY clientsChanged)
    Q_PROPERTY(QVariantList activity READ activity NOTIFY activityChanged)

public:
    explicit McpController(ProjectController* project, QObject* parent = nullptr, QString discoveryFile = {});
    ~McpController() override;
    bool enabled() const;
    QString status() const { return status_; }
    QString endpoint() const;
    QString bridgePath() const;
    QVariantList clients() const;
    QVariantList activity() const { return activity_; }
    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void approve(const QString& sessionId, const QString& access);
    Q_INVOKABLE void revoke(const QString& sessionId);
    /// Ends the session and refuses new ones from this assistant until access is turned off and on.
    Q_INVOKABLE void deny(const QString& sessionId);
    Q_INVOKABLE int undoEdits(const QString& sessionId);
    Q_INVOKABLE void copySetup();
    /// Adds the tools that need the app (recent projects, opening one).
    void enableAppTools(McpAppHooks hooks);
    /// <app data>/Lectern/mcp/connection.json (under LECTERN_APP_DATA_DIR when that is set).
    static QString defaultDiscoveryFile();

signals:
    void stateChanged();
    void clientsChanged();
    void activityChanged();

private:
    void acceptConnections();
    void receive(QTcpSocket* socket);
    void log(const QString& client, const QString& action, const QString& outcome);
    void stop();
    ProjectController* project_;
    QTcpServer* listener_;
    mcp::Server server_;
    std::map<std::string, mcp::Session> sessions_;
    std::map<std::string, std::uint64_t> opened_;  ///< session id → order it was opened in
    std::uint64_t openedCount_ = 0;
    std::set<std::string> denied_;                 ///< assistant names the user denied
    QString discoveryFile_;
    std::unique_ptr<QLockFile> discoveryLock_;
    QByteArray token_;
    QString status_ = QStringLiteral("Assistant access is off");
    QVariantList activity_;
};

void registerMcpTools(mcp::Server& server, ProjectController* project);
/// list_projects and open_project (the headless bridge has no recent projects).
void registerMcpAppTools(mcp::Server& server, McpAppHooks hooks);

} // namespace lectern::ui
