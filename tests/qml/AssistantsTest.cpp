// The approval request that appears when an AI assistant connects: Read and
// edit / Read only / Deny, answered on the window, one assistant at a time.

#include "qml/QmlHarness.h"

#include "ui/McpController.h"
#include "ui/ProjectController.h"

#include <QDir>
#include <QImage>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimer>

#include <gtest/gtest.h>

#include <cstdlib>

using namespace lectern;
using test::QmlHarness;
using mcp::Json;

namespace {

const QByteArray kWindow = R"(
import QtQuick
import Lectern.UI
Item {
    required property McpController assistants
    AssistantRequest { assistants: parent.assistants }
}
)";

/// An assistant saying hello to the endpoint; returns the HTTP status.
int connectAssistant(ui::McpController& endpoint, const QString& connectionFile, const std::string& name) {
    QFile file(connectionFile);
    if (!file.open(QIODevice::ReadOnly)) return -1;
    const auto token = QByteArray::fromStdString(Json::parse(file.readAll().toStdString())["token"].get<std::string>());
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(endpoint.endpoint())};
    request.setRawHeader("Authorization", "Bearer " + token);
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("Content-Type", "application/json");
    const Json hello{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                     {"params", {{"protocolVersion", "2025-11-25"}, {"capabilities", Json::object()},
                                 {"clientInfo", {{"name", name}, {"version", "1"}}}}}};
    QNetworkReply* reply = network.post(request, QByteArray::fromStdString(hello.dump()));
    QEventLoop wait;
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    QTimer::singleShot(5000, reply, &QNetworkReply::abort);
    wait.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    return status;
}

}  // namespace

TEST(Assistants, ConnectingAssistantAsksForApprovalOnTheWindow) {
    QTemporaryDir dir;
    const QString connection = dir.filePath(QStringLiteral("mcp/connection.json"));
    ui::ProjectController project;
    ui::McpController endpoint(&project, nullptr, connection);
    endpoint.setEnabled(true);
    ASSERT_TRUE(endpoint.enabled());
    QmlHarness ui(kWindow, {{QStringLiteral("assistants"), QVariant::fromValue(&endpoint)}}, QSize(900, 600));
    ASSERT_TRUE(ui.ok());
    auto find = [&](const char* name) { return QmlHarness::findInTree(ui.window().contentItem(), QString::fromLatin1(name)); };
    auto center = [&](const char* name) {
        QQuickItem* item = find(name);
        EXPECT_TRUE(item) << name;
        return item ? QmlHarness::at(item, {item->width() / 2, item->height() / 2}) : QPoint();
    };
    // The request is a popup: its buttons show while an assistant waits.
    auto asking = [&] {
        QQuickItem* button = find("requestEdit");
        return button && button->isVisible();
    };
    EXPECT_FALSE(asking());  // nothing to ask yet

    ASSERT_EQ(connectAssistant(endpoint, connection, "Claude"), 200);
    test::settle(80);
    ASSERT_TRUE(asking());
    if (const char* dump = std::getenv("LECTERN_DUMP_DIR")) ui.window().grabWindow().save(QString::fromUtf8(dump) + "/assistant-request.png");
    ui.click(center("requestEdit"));
    ASSERT_EQ(endpoint.clients().size(), 1);
    EXPECT_EQ(endpoint.clients().front().toMap().value("access").toString(), "edit");
    test::settle(80);
    EXPECT_FALSE(asking());

    // A second assistant: read only.
    ASSERT_EQ(connectAssistant(endpoint, connection, "Codex"), 200);
    test::settle(80);
    ASSERT_TRUE(asking());
    ui.click(center("requestRead"));
    QStringList levels;
    for (const auto& c : endpoint.clients()) levels << c.toMap().value("name").toString() + "=" + c.toMap().value("access").toString();
    levels.sort();
    EXPECT_EQ(levels.join(","), "Claude=edit,Codex=read");

    // A third one is denied, and stays out.
    ASSERT_EQ(connectAssistant(endpoint, connection, "Unknown tool"), 200);
    test::settle(80);
    ui.click(center("requestDeny"));
    EXPECT_EQ(endpoint.clients().size(), 2);
    EXPECT_EQ(connectAssistant(endpoint, connection, "Unknown tool"), 403);
    test::settle(80);
    EXPECT_FALSE(asking());
}
