// The real lectern-mcp bridge, started as an assistant starts it (a child
// process speaking MCP on stdin/stdout), against the app's endpoint
// (McpController): approval, Lectern starting late or restarting, deny,
// session end, and headless edits that survive the bridge being killed.

#include "editor/EditorFixture.h"
#include "project/ProjectStore.h"
#include "ui/McpController.h"
#include "ui/ProjectController.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>

using namespace lectern;
using namespace lectern::ui;
using mcp::Json;

namespace {

bool waitFor(const std::function<bool()>& done, int timeoutMs = 10000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        QEventLoop loop;  // the host's endpoint runs on this thread: keep it answering
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

QString bridgeBinary() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/lectern-mcp");
}

/// lectern-mcp as a child process.
struct Bridge {
    QProcess process;
    QByteArray buffer;
    int nextId = 1;

    Bridge(const QStringList& args, const QString& appData) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LECTERN_APP_DATA_DIR"), appData);  // never the user's own endpoint
        process.setProcessEnvironment(env);
        process.setProgram(bridgeBinary());
        process.setArguments(args);
        process.start();
        EXPECT_TRUE(process.waitForStarted(5000));
    }
    ~Bridge() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(2000);
        }
    }
    void send(const Json& message) { process.write(QByteArray::fromStdString(message.dump()) + '\n'); }
    void sendRaw(const QByteArray& line) { process.write(line + '\n'); }
    /// The next line the bridge writes.
    Json read(int timeoutMs = 15000) {
        const bool got = waitFor([&] {
            buffer += process.readAllStandardOutput();
            return buffer.contains('\n');
        }, timeoutMs);
        if (!got) {
            ADD_FAILURE() << "no reply; stderr: " << process.readAllStandardError().toStdString();
            return Json();
        }
        const auto end = buffer.indexOf('\n');
        const QByteArray line = buffer.left(end);
        buffer.remove(0, end + 1);
        return Json::parse(line.toStdString());
    }
    Json request(const std::string& method, Json params = Json::object()) {
        send({{"jsonrpc", "2.0"}, {"id", nextId++}, {"method", method}, {"params", params}});
        return read();
    }
    Json initialize(const std::string& name = "Bridge test") {
        const Json reply = request("initialize", {{"protocolVersion", "2025-06-18"}, {"capabilities", Json::object()},
                                                  {"clientInfo", {{"name", name}, {"version", "1"}}}});
        send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
        return reply;
    }
    Json call(const std::string& tool, Json args = Json::object()) {
        return request("tools/call", {{"name", tool}, {"arguments", args}});
    }
};

bool isError(const Json& reply) { return reply.contains("error") || reply["result"].value("isError", false); }
std::string text(const Json& reply) {
    if (reply.contains("error")) return reply["error"].value("message", std::string());
    return reply["result"]["content"][0].value("text", std::string());
}

/// The app's side: a project and the assistant endpoint, as AppController owns them.
struct Host {
    test::EditorFixture fixture;
    ProjectController controller;
    QString appData = QString::fromStdString((fixture.dir.path() / "appdata").string());
    QString connection = appData + QStringLiteral("/Lectern/mcp/connection.json");
    McpController endpoint{&controller, nullptr, connection};

    Host() {
        EXPECT_TRUE(project::ProjectStore::save(fixture.dir.path(), fixture.project));
        QEventLoop wait;
        QObject::connect(&controller, &ProjectController::opened, &wait, &QEventLoop::quit);
        QTimer::singleShot(10000, &wait, &QEventLoop::quit);
        controller.open(QString::fromStdString(fixture.dir.path().string()));
        wait.exec();
        EXPECT_TRUE(controller.loaded());
    }
    QVariantMap only() {
        const auto clients = endpoint.clients();
        return clients.size() == 1 ? clients.front().toMap() : QVariantMap();
    }
};

#define SKIP_WITHOUT_BRIDGE() \
    if (!QFileInfo::exists(bridgeBinary())) GTEST_SKIP() << "lectern-mcp is not built next to the tests"

}  // namespace

TEST(McpBridge, StartsBeforeLecternAndConnectsOnceItIsOn) {
    SKIP_WITHOUT_BRIDGE();
    Host host;
    Bridge bridge({QStringLiteral("--connection-file"), host.connection}, host.appData);
    // Lectern's assistant access is off: the bridge answers the handshake and the lists itself.
    const Json hello = bridge.initialize();
    ASSERT_TRUE(hello.contains("result")) << hello.dump();
    EXPECT_EQ(hello["result"]["protocolVersion"], "2025-06-18");
    EXPECT_EQ(hello["result"]["serverInfo"]["name"], "lectern");
    const Json tools = bridge.request("tools/list");
    EXPECT_GE(tools["result"]["tools"].size(), 49u);
    Json reply = bridge.call("get_timeline");
    EXPECT_TRUE(isError(reply));
    EXPECT_NE(text(reply).find("Allow AI Assistants"), std::string::npos) << text(reply);
    EXPECT_EQ(bridge.request("ping")["result"], Json::object());

    // Turned on: the next call opens a session, which waits for the user's approval.
    host.endpoint.setEnabled(true);
    ASSERT_TRUE(host.endpoint.enabled());
    reply = bridge.call("get_timeline");
    EXPECT_TRUE(isError(reply));
    EXPECT_NE(text(reply).find("approval"), std::string::npos) << text(reply);
    ASSERT_EQ(host.only().value("access").toString(), "pending");
    EXPECT_EQ(host.only().value("name").toString(), "Bridge test");
    const QString id = host.only().value("id").toString();

    host.endpoint.approve(id, QStringLiteral("read"));
    reply = bridge.call("get_timeline");
    ASSERT_FALSE(isError(reply)) << reply.dump();
    EXPECT_TRUE(reply["result"]["structuredContent"].contains("tracks"));
    EXPECT_TRUE(isError(bridge.call("add_marker", {{"time", 1.0}, {"label", "Bridge"}})));  // read only
    host.endpoint.approve(id, QStringLiteral("edit"));
    reply = bridge.call("add_marker", {{"time", 1.0}, {"label", "Bridge"}});
    ASSERT_FALSE(isError(reply)) << reply.dump();
    EXPECT_EQ(host.controller.snapshot()->timeline.markers.back().label, "Bridge");
}

TEST(McpBridge, SurvivesALecternRestartAndEndsItsSessionOnExit) {
    SKIP_WITHOUT_BRIDGE();
    Host host;
    host.endpoint.setEnabled(true);
    Bridge bridge({QStringLiteral("--connection-file"), host.connection}, host.appData);
    ASSERT_TRUE(bridge.initialize().contains("result"));
    // Lectern is on: the assistant asks for approval as soon as it connects.
    ASSERT_TRUE(waitFor([&] { return host.endpoint.clients().size() == 1; }));
    host.endpoint.approve(host.only().value("id").toString(), QStringLiteral("edit"));
    ASSERT_FALSE(isError(bridge.call("get_timeline")));

    // A new endpoint (Lectern restarted: new port and token): one new approval, no restart of the assistant.
    host.endpoint.setEnabled(false);
    host.endpoint.setEnabled(true);
    EXPECT_TRUE(host.endpoint.clients().isEmpty());
    Json reply = bridge.call("get_timeline");
    EXPECT_TRUE(isError(reply));
    ASSERT_EQ(host.only().value("access").toString(), "pending");
    host.endpoint.approve(host.only().value("id").toString(), QStringLiteral("edit"));
    reply = bridge.call("add_marker", {{"time", 2.0}, {"label", "After restart"}});
    ASSERT_FALSE(isError(reply)) << reply.dump();
    EXPECT_EQ(host.controller.snapshot()->timeline.markers.back().label, "After restart");

    // Ending the session in Lectern (Revoke) is the same: a new one, approved again.
    host.endpoint.revoke(host.only().value("id").toString());
    EXPECT_TRUE(isError(bridge.call("get_timeline")));
    EXPECT_EQ(host.only().value("access").toString(), "pending");

    // The assistant quits: the bridge ends its session and exits cleanly.
    bridge.process.closeWriteChannel();
    ASSERT_TRUE(waitFor([&] { return bridge.process.state() == QProcess::NotRunning; }));
    EXPECT_EQ(bridge.process.exitCode(), 0);
    EXPECT_TRUE(host.endpoint.clients().isEmpty());
}

TEST(McpBridge, TerminatedBridgeEndsItsSessionToo) {
    SKIP_WITHOUT_BRIDGE();
#ifdef Q_OS_WIN
    GTEST_SKIP() << "no SIGTERM on Windows";
#endif
    Host host;
    host.endpoint.setEnabled(true);
    Bridge bridge({QStringLiteral("--connection-file"), host.connection}, host.appData);
    ASSERT_TRUE(bridge.initialize().contains("result"));
    ASSERT_TRUE(waitFor([&] { return host.endpoint.clients().size() == 1; }));
    bridge.process.terminate();  // SIGTERM, as assistants stop their servers
    ASSERT_TRUE(waitFor([&] { return bridge.process.state() == QProcess::NotRunning; }));
    EXPECT_TRUE(waitFor([&] { return host.endpoint.clients().isEmpty(); }, 3000));
}

TEST(McpBridge, DeniedAssistantStaysOutUntilAccessIsTurnedOffAndOn) {
    SKIP_WITHOUT_BRIDGE();
    Host host;
    host.endpoint.setEnabled(true);
    Bridge bridge({QStringLiteral("--connection-file"), host.connection}, host.appData);
    ASSERT_TRUE(bridge.initialize("Unwanted").contains("result"));
    ASSERT_TRUE(waitFor([&] { return host.endpoint.clients().size() == 1; }));
    host.endpoint.deny(host.only().value("id").toString());
    const Json reply = bridge.call("get_timeline");
    EXPECT_TRUE(isError(reply));
    EXPECT_NE(text(reply).find("denied"), std::string::npos) << text(reply);
    EXPECT_TRUE(host.endpoint.clients().isEmpty());  // no new approval request after a denial
    host.endpoint.setEnabled(false);
    host.endpoint.setEnabled(true);
    EXPECT_TRUE(isError(bridge.call("get_timeline")));  // asks again, as a new request
    EXPECT_EQ(host.only().value("access").toString(), "pending");
}

TEST(McpBridge, ProbesAndBadLinesDoNotEndTheBridge) {
    SKIP_WITHOUT_BRIDGE();
    Host host;
    host.endpoint.setEnabled(true);
    Bridge bridge({QStringLiteral("--connection-file"), host.connection}, host.appData);
    // The 2026 stateless-era probe comes first from newer clients: "not found" → they fall back.
    EXPECT_EQ(bridge.request("server/discover", {{"protocolVersion", "2026-07-28"}})["error"]["code"], -32601);
    bridge.sendRaw("{not json");
    EXPECT_EQ(bridge.read()["error"]["code"], -32700);
    ASSERT_TRUE(bridge.initialize().contains("result"));
    EXPECT_EQ(bridge.request("initialize", {{"protocolVersion", "2025-06-18"}, {"capabilities", Json::object()},
                                            {"clientInfo", {{"name", "x"}, {"version", "1"}}}})["error"]["code"], -32600);
    EXPECT_EQ(bridge.process.state(), QProcess::Running);
    EXPECT_EQ(host.endpoint.clients().size(), 1);
}

TEST(McpBridge, HeadlessEditsAreOnDiskBeforeTheReply) {
    SKIP_WITHOUT_BRIDGE();
    test::EditorFixture fixture;
    ASSERT_TRUE(project::ProjectStore::save(fixture.dir.path(), fixture.project));
    const QString appData = QString::fromStdString((fixture.dir.path() / "appdata").string());
    Bridge bridge({QStringLiteral("--project"), QString::fromStdString(fixture.dir.path().string()), QStringLiteral("--allow-edits")}, appData);
    ASSERT_TRUE(bridge.initialize().contains("result"));
    const Json reply = bridge.call("add_marker", {{"time", 1.5}, {"label", "Durable"}});
    ASSERT_FALSE(isError(reply)) << reply.dump();
    bridge.process.kill();  // gone without any chance to save (SIGKILL)
    bridge.process.waitForFinished(3000);
    const auto saved = project::ProjectStore::load(fixture.dir.path());
    ASSERT_TRUE(saved) << saved.error().toString();
    ASSERT_FALSE(saved->project.timeline.markers.empty());
    EXPECT_EQ(saved->project.timeline.markers.back().label, "Durable");
}

// Any real MCP client through the bridge and the app's endpoint, e.g. the
// official Python SDK (tests/mcp/sdk_client.py) or Claude Code:
//   LECTERN_TEST_MCP_CLIENT='claude -p "Add a marker named real-client at 1 s with the lectern tools." \
//       --mcp-config "$LECTERN_TEST_MCP_CONFIG" --strict-mcp-config --allowedTools mcp__lectern__add_marker'
// The command runs in a shell with $LECTERN_TEST_MCP_CONFIG set to the assistant
// setup; the test plays the user and approves each assistant that asks. The
// client has to add a marker named "real-client".
TEST(McpBridge, RealClientThroughTheApp) {
    const QByteArray command = qgetenv("LECTERN_TEST_MCP_CLIENT");
    if (command.isEmpty()) GTEST_SKIP() << "set LECTERN_TEST_MCP_CLIENT to an MCP client command";
    SKIP_WITHOUT_BRIDGE();
    Host host;
    host.endpoint.setEnabled(true);
    ASSERT_TRUE(host.endpoint.enabled());
    const QString config = host.appData + QStringLiteral("/assistant.json");
    {
        QFile file(config);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        const Json setup{{"mcpServers", {{"lectern", {{"command", bridgeBinary().toStdString()},
                                                     {"args", {"--connection-file", host.connection.toStdString()}}}}}}};
        file.write(QByteArray::fromStdString(setup.dump(2)));
    }
    QProcess client;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LECTERN_TEST_MCP_CONFIG"), config);
    env.insert(QStringLiteral("LECTERN_APP_DATA_DIR"), host.appData);
    client.setProcessEnvironment(env);
    client.setProcessChannelMode(QProcess::MergedChannels);
    client.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QString::fromLocal8Bit(command)});
    ASSERT_TRUE(client.waitForStarted(5000));
    int approved = 0;
    const bool finished = waitFor([&] {
        for (const auto& c : host.endpoint.clients()) {
            const QVariantMap m = c.toMap();
            if (m.value("access") == QLatin1String("pending")) {
                host.endpoint.approve(m.value("id").toString(), QStringLiteral("edit"));
                ++approved;
            }
        }
        return client.state() == QProcess::NotRunning;
    }, 300000);
    if (!finished) client.kill();
    std::printf("--- client output ---\n%s\n--- assistant activity ---\n", client.readAll().constData());
    for (const auto& a : host.endpoint.activity()) {
        const QVariantMap m = a.toMap();
        std::printf("%s %s: %s -> %s\n", qPrintable(m.value("time").toString()), qPrintable(m.value("client").toString()),
                    qPrintable(m.value("action").toString()), qPrintable(m.value("outcome").toString()));
    }
    ASSERT_TRUE(finished) << "the client did not finish";
    EXPECT_EQ(client.exitCode(), 0);
    EXPECT_GE(approved, 1);
    const auto& markers = host.controller.snapshot()->timeline.markers;
    EXPECT_TRUE(std::any_of(markers.begin(), markers.end(), [](const auto& m) { return m.label == "real-client"; }));
}
