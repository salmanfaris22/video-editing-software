#include "editor/EditorFixture.h"
#include "project/ProjectStore.h"
#include "ui/McpController.h"
#include "ui/ProjectController.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::ui;
using mcp::Json;
namespace {
Json request(std::string method, Json params = Json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}};
}
Json init() {
    return request("initialize", {{"protocolVersion", "2025-11-25"}, {"capabilities", Json::object()}, {"clientInfo", {{"name", "Test assistant"}, {"version", "1"}}}});
}

struct Project {
    test::EditorFixture fixture;
    ProjectController controller;
    mcp::Server server;
    mcp::Session session{.id = "test", .access = mcp::Access::Edit};
    Project() {
        EXPECT_TRUE(project::ProjectStore::save(fixture.dir.path(), fixture.project));
        QEventLoop wait;
        QObject::connect(&controller, &ProjectController::opened, &wait, &QEventLoop::quit);
        QTimer::singleShot(10000, &wait, &QEventLoop::quit);
        controller.open(QString::fromStdString(fixture.dir.path().string()));
        wait.exec();
        EXPECT_TRUE(controller.loaded());
        registerMcpTools(server, &controller);
        (void)server.handle(init(), session);
        (void)server.handle(Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, session);
    }
    Json call(const std::string& name, Json args = Json::object()) {
        const auto response = server.handle(request("tools/call", {{"name", name}, {"arguments", args}}), session);
        EXPECT_TRUE(response);
        return response.value_or(Json::object());
    }
    std::string clip() { return controller.snapshot()->timeline.tracks.front().clips.front().id.toString(); }
};
} // namespace

TEST(McpUi, ToolsEditTheRealProjectAndUndoInOneStep) {
    Project p;
    const auto before = *p.controller.snapshot();
    auto reply = p.call("set_color", {{"clipId", p.clip()}, {"values", {{"exposure", 0.4}, {"contrast", 0.3}}}});
    ASSERT_FALSE(reply["result"]["isError"].get<bool>()) << reply;
    EXPECT_EQ(p.controller.undoLabel(), "Assistant: set_color");
    EXPECT_DOUBLE_EQ(p.controller.snapshot()->timeline.tracks.front().clips.front().color.exposure.value, 0.4);
    p.controller.undo();
    EXPECT_EQ(*p.controller.snapshot(), before);
    EXPECT_FALSE(p.controller.canUndo());
    p.controller.redo();
    EXPECT_DOUBLE_EQ(p.controller.snapshot()->timeline.tracks.front().clips.front().color.contrast.value, 0.3);
    p.call("split_at", {{"time", 1.0}});
    EXPECT_EQ(p.controller.snapshot()->timeline.tracks.front().clips.size(), 2u);
    p.call("undo");
    EXPECT_EQ(p.controller.snapshot()->timeline.tracks.front().clips.size(), 1u);
    p.call("redo");
    EXPECT_EQ(p.controller.snapshot()->timeline.tracks.front().clips.size(), 2u);
}

TEST(McpUi, FailedCompoundEditRollsBackAndCannotDeletePopulatedLayer) {
    Project p;
    const auto before = *p.controller.snapshot();
    const auto failed = p.call("set_audio", {{"clipId", p.clip()}, {"values", {{"gainDb", -10}, {"solo", true}}}});
    EXPECT_TRUE(failed["result"]["isError"].get<bool>());
    EXPECT_EQ(*p.controller.snapshot(), before);
    EXPECT_FALSE(p.controller.canUndo());
    const auto layer = p.controller.snapshot()->timeline.tracks.front().id.toString();
    EXPECT_TRUE(p.call("delete_layer", {{"layerId", layer}})["result"]["isError"].get<bool>());
    EXPECT_EQ(*p.controller.snapshot(), before);
    EXPECT_TRUE(p.call("apply_lut", {{"clipId", p.clip()}, {"lut", "/tmp/private.cube"}})["result"]["isError"].get<bool>());
    EXPECT_EQ(*p.controller.snapshot(), before);
}

TEST(McpUi, AssistantUndoStopsAtHumanEditsAndSessionsAreIsolated) {
    Project p;
    p.call("add_marker", {{"time", 1}, {"label", "First"}});
    p.controller.addMarker(1.5, QStringLiteral("Human"));
    p.call("add_marker", {{"time", 2}, {"label", "Second"}});
    EXPECT_EQ(p.controller.undoAssistantEdits(QStringLiteral("someone-else")), 0);
    EXPECT_EQ(p.controller.undoAssistantEdits(QStringLiteral("test")), 1);
    ASSERT_EQ(p.controller.snapshot()->timeline.markers.size(), 2u);
    EXPECT_EQ(p.controller.snapshot()->timeline.markers.back().label, "Human");
    EXPECT_EQ(p.controller.undoAssistantEdits(QStringLiteral("test")), 0);
}

TEST(McpUi, ToolCoverageAndSchemaRejection) {
    Project p;
    const auto clip = p.clip();
    for (const auto* name : {"get_project", "get_timeline", "get_selection", "list_presets", "get_pauses"})
        EXPECT_FALSE(p.call(name)["result"]["isError"].get<bool>()) << name;
    const std::vector<std::pair<std::string, Json>> edits{
        {"add_text", {{"text", "Hello"}, {"time", 0}, {"duration", 1}}},
        {"add_subtitle", {{"text", "Caption"}, {"time", 0}, {"duration", 1}}},
        {"set_color_wheel", {{"clipId", clip}, {"wheel", "lift"}, {"x", 0.1}, {"y", 0.2}, {"master", 0.1}}},
        {"reset_color", {{"clipId", clip}}},
        {"set_effect", {{"clipId", clip}, {"type", "blur"}, {"enabled", true}, {"params", {{"amount", 0.3}}}}},
        {"set_style", {{"values", {{"screenRadius", 0.04}}}}},
        {"set_clip_transform", {{"clipId", clip}, {"opacity", 0.6}}},
        {"trim_clip", {{"clipId", clip}, {"edge", "end"}, {"time", 2.5}}},
        {"remove_range", {{"start", 1}, {"end", 1.5}}}
    };
    for (const auto& [name, args] : edits) {
        const auto response = p.call(name, args);
        ASSERT_TRUE(response.contains("result")) << name << ": " << response;
        EXPECT_FALSE(response["result"]["isError"].get<bool>()) << name << ": " << response;
    }
    const auto before = *p.controller.snapshot();
    EXPECT_EQ(p.call("trim_clip", {{"clipId", clip}, {"edge", "oops"}, {"time", 1}})["error"]["code"], -32602);
    EXPECT_EQ(p.call("set_style", {{"values", {{"screenRadius", 20}}}})["error"]["code"], -32602);
    EXPECT_EQ(p.call("add_marker", {{"time", -1}, {"label", "Bad"}})["error"]["code"], -32602);
    EXPECT_TRUE(p.call("move_clip", {{"clipId", "missing"}, {"time", 0}})["result"]["isError"].get<bool>());
    EXPECT_EQ(*p.controller.snapshot(), before);
}

TEST(McpUi, HttpRequiresTokenOriginAndPerClientConsent) {
    Project p;
    const auto file = QString::fromStdString((p.fixture.dir.path() / "mcp" / "connection.json").string());
    McpController host(&p.controller, nullptr, file);
    EXPECT_FALSE(host.enabled());
    host.setEnabled(true);
    ASSERT_TRUE(host.enabled()) << host.status().toStdString();
    QFile connection(file); ASSERT_TRUE(connection.open(QIODevice::ReadOnly));
    const auto config = Json::parse(connection.readAll().toStdString());
    const auto token = QByteArray::fromStdString(config["token"].get<std::string>());
    QByteArray session;
    QNetworkAccessManager network;
    struct Reply { int status; Json body; QByteArray session; };
    const auto post = [&](const Json& body, QByteArray auth, QByteArray origin = {}) {
        QNetworkRequest request(QUrl(host.endpoint()));
        request.setRawHeader("Authorization", "Bearer " + auth);
        request.setRawHeader("Accept", "application/json, text/event-stream");
        request.setRawHeader("Content-Type", "application/json");
        if (!origin.isEmpty()) request.setRawHeader("Origin", origin);
        if (!session.isEmpty()) { request.setRawHeader("Mcp-Session-Id", session); request.setRawHeader("MCP-Protocol-Version", "2025-11-25"); }
        auto* response = network.post(request, QByteArray::fromStdString(body.dump()));
        QEventLoop wait;
        QObject::connect(response, &QNetworkReply::finished, &wait, &QEventLoop::quit);
        QTimer::singleShot(5000, response, &QNetworkReply::abort);
        wait.exec();
        const auto bytes = response->readAll();
        Reply out{response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), bytes.isEmpty() ? Json() : Json::parse(bytes.toStdString()), response->rawHeader("Mcp-Session-Id")};
        response->deleteLater();
        return out;
    };
    EXPECT_EQ(post(init(), "wrong").status, 401);
    EXPECT_EQ(post(init(), token, "https://untrusted.example").status, 403);
    const auto initialized = post(init(), token);
    ASSERT_EQ(initialized.status, 200);
    ASSERT_FALSE(initialized.session.isEmpty());
    session = initialized.session;
    EXPECT_EQ(post(Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, token).status, 202);
    auto call = request("tools/call", {{"name", "get_timeline"}});
    EXPECT_TRUE(post(call, token).body["result"]["isError"].get<bool>());
    host.approve(QString::fromUtf8(session), QStringLiteral("read"));
    EXPECT_FALSE(post(call, token).body["result"]["isError"].get<bool>());
    call = request("tools/call", {{"name", "add_marker"}, {"arguments", {{"time", 1}, {"label", "HTTP"}}}});
    EXPECT_TRUE(post(call, token).body["result"]["isError"].get<bool>());
    host.approve(QString::fromUtf8(session), QStringLiteral("edit"));
    EXPECT_FALSE(post(call, token).body["result"]["isError"].get<bool>());
    EXPECT_EQ(p.controller.snapshot()->timeline.markers.back().label, "HTTP");
    host.revoke(QString::fromUtf8(session));
    EXPECT_EQ(post(call, token).status, 404);
    host.setEnabled(false);
    EXPECT_FALSE(QFileInfo::exists(file));
}

TEST(McpUi, RenderFrameReturnsAPngOfTheEdit) {
    Project p;
    const auto r = p.call("render_frame", {{"time", 1.0}, {"width", 320}});
    ASSERT_TRUE(r.contains("result")) << r;
    const auto& content = r["result"]["content"];
    ASSERT_EQ(content[0]["type"], "image");
    EXPECT_EQ(content[0]["mimeType"], "image/png");
    const QImage image = QImage::fromData(QByteArray::fromBase64(QByteArray::fromStdString(content[0]["data"].get<std::string>())), "PNG");
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.width(), 320);
    EXPECT_EQ(image.height(), 320 * p.fixture.project.canvas.height / p.fixture.project.canvas.width);
    EXPECT_EQ(r["result"]["structuredContent"]["width"], 320);
}

TEST(McpUi, ExportRunsInTheBackgroundIntoTheProjectFolderOnly) {
    Project p;
    // Exporting needs full access.
    EXPECT_TRUE(p.call("export_video", {{"name", "Lesson"}})["result"]["isError"].get<bool>());
    p.session.access = mcp::Access::Full;
    const auto started = p.call("export_video", {{"name", "../../Lesson/x"}, {"resolution", "720p"}, {"quality", "draft"}});
    ASSERT_FALSE(started["result"]["isError"].get<bool>()) << started;
    const auto& info = started["result"]["structuredContent"];
    const QString output = QString::fromStdString(info["output"].get<std::string>());
    EXPECT_TRUE(output.startsWith(QString::fromStdString((p.fixture.dir / "Exports").string()))) << output.toStdString();
    EXPECT_TRUE(output.endsWith(QStringLiteral("x.mp4"))) << output.toStdString();  // path parts are dropped
    const std::string job = info["jobId"];
    std::string state = "running";
    for (int i = 0; i < 600 && state == "running"; ++i) {
        QEventLoop pause;
        QTimer::singleShot(50, &pause, &QEventLoop::quit);
        pause.exec();
        state = p.call("get_export", {{"jobId", job}})["result"]["structuredContent"]["state"];
    }
    EXPECT_EQ(state, "done");
    EXPECT_TRUE(QFileInfo(output).size() > 1000);
    EXPECT_TRUE(p.call("get_export", {{"jobId", "nope"}})["result"]["isError"].get<bool>());
}

TEST(McpUi, SubtitlesRoundTripThroughTheProjectFolder) {
    Project p;
    p.session.access = mcp::Access::Full;
    ASSERT_FALSE(p.call("add_subtitle", {{"text", "First line"}, {"time", 0.2}, {"duration", 1}})["result"]["isError"].get<bool>());
    const auto out = p.call("export_subtitles", {{"name", "captions"}, {"format", "srt"}});
    ASSERT_FALSE(out["result"]["isError"].get<bool>()) << out;
    const QString path = QString::fromStdString(out["result"]["structuredContent"]["output"].get<std::string>());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("First line"));
    file.close();
    p.controller.clearSubtitles();
    EXPECT_TRUE(p.controller.subtitles().isEmpty());
    const auto in = p.call("import_subtitles", {{"path", "Exports/captions.srt"}});
    ASSERT_FALSE(in["result"]["isError"].get<bool>()) << in.dump();
    EXPECT_EQ(p.controller.subtitles().size(), 1);
    // Files outside the project folder are refused.
    EXPECT_TRUE(p.call("import_subtitles", {{"path", "/etc/hosts"}})["result"]["isError"].get<bool>());
    EXPECT_TRUE(p.call("import_subtitles", {{"path", "../outside.srt"}})["result"]["isError"].get<bool>());
}

TEST(McpUi, AppToolsListAndOpenOnlyRecentProjects) {
    Project p;
    p.session.access = mcp::Access::Full;
    QString opened;
    const QString path = QString::fromStdString(p.fixture.dir.path().string());
    registerMcpAppTools(p.server, {[&] { return QVariantList{QVariantMap{{"path", path}, {"name", "Lesson"}}}; },
                                    [&](const QString& dir) { opened = dir; }});
    const auto list = p.call("list_projects");
    ASSERT_FALSE(list["result"]["isError"].get<bool>()) << list;
    EXPECT_EQ(list["result"]["structuredContent"]["projects"].size(), 1U);
    EXPECT_TRUE(p.call("open_project", {{"path", "/tmp/elsewhere"}})["result"]["isError"].get<bool>());
    EXPECT_TRUE(opened.isEmpty());
    EXPECT_FALSE(p.call("open_project", {{"path", path.toStdString()}})["result"]["isError"].get<bool>());
    EXPECT_EQ(opened, path);
}

TEST(McpUi, SetClipColorSpaceIsUndoableAndValidated) {
    Project p;
    const auto clip = p.clip();
    const auto r = p.call("set_clip_color_space", {{"clipId", clip}, {"space", "rec2020-hlg"}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    p.controller.selectClip(QString::fromStdString(clip));
    EXPECT_EQ(p.controller.selection().value("inputColorSpace").toString(), "rec2020-hlg");
    EXPECT_EQ(p.call("set_clip_color_space", {{"clipId", clip}, {"space", "rec2100"}})["error"]["code"], -32602);
    p.controller.undo();
    p.controller.selectClip(QString::fromStdString(clip));
    EXPECT_EQ(p.controller.selection().value("inputColorSpace").toString(), "auto");
    EXPECT_EQ(p.controller.selection().value("detectedColorSpace").toString(), "Rec.709");
}

TEST(McpUi, GetScopesMeasuresTheGradedPicture) {
    Project p;
    const auto before = p.call("get_scopes", {{"time", 1.0}});
    ASSERT_FALSE(before["result"]["isError"].get<bool>()) << before;
    const double luma0 = before["result"]["structuredContent"]["averageLuma"];
    // Brighten every visible clip; the measured average luma goes up.
    for (const auto& track : p.controller.snapshot()->timeline.tracks)
        for (const auto& clip : track.clips)
            if (track.kind == timeline::TrackKind::Video) p.controller.setColorValue(QString::fromStdString(clip.id.toString()), "exposure", 1.0);
    const auto after = p.call("get_scopes", {{"time", 1.0}});
    EXPECT_GT(after["result"]["structuredContent"]["averageLuma"].get<double>(), luma0 + 30);
    EXPECT_LE(after["result"]["structuredContent"]["white"]["r"].get<int>(), 1023);
}

TEST(McpUi, SetCurveShapesTheGradeAndValidatesPoints) {
    Project p;
    const auto clip = p.clip();
    const auto r = p.call("set_curve", {{"clipId", clip}, {"channel", "y"}, {"points", {{0, 0}, {0.5, 0.65}, {1, 1}}}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    p.controller.selectClip(QString::fromStdString(clip));
    ASSERT_EQ(p.controller.selection().value("curveY").toList().size(), 3);
    EXPECT_EQ(p.call("set_curve", {{"clipId", clip}, {"channel", "y"}, {"points", {{0, 2}}}})["error"]["code"], -32602);
    EXPECT_EQ(p.call("set_curve", {{"clipId", clip}, {"channel", "x"}, {"points", Json::array()}})["error"]["code"], -32602);
    p.call("set_curve", {{"clipId", clip}, {"channel", "y"}, {"points", Json::array()}});  // removes it
    p.controller.selectClip(QString::fromStdString(clip));
    EXPECT_TRUE(p.controller.selection().value("curveY").toList().isEmpty());
}

TEST(McpUi, LooksAndCopyGradeThroughTools) {
    Project p;
    const auto& tracks = p.controller.snapshot()->timeline.tracks;
    std::vector<std::string> clips;
    for (const auto& t : tracks) {
        if (t.kind != timeline::TrackKind::Video && t.kind != timeline::TrackKind::Overlay) continue;
        for (const auto& c : t.clips) clips.push_back(c.id.toString());
    }
    ASSERT_GE(clips.size(), 2u);
    const auto list = p.call("list_looks", Json::object());
    ASSERT_FALSE(list["result"]["isError"].get<bool>()) << list;
    const auto& looks = list["result"]["structuredContent"]["looks"];
    ASSERT_GE(looks.size(), 16u) << list;

    auto r = p.call("apply_look", {{"clipIds", {clips[0]}}, {"look", "oppenheimer"}, {"amount", 0.6}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    p.controller.selectClip(QString::fromStdString(clips[0]));
    EXPECT_EQ(p.controller.selection().value("look").toString(), "oppenheimer");
    EXPECT_NEAR(p.controller.selection().value("lookAmount").toDouble(), 0.6, 1e-9);
    EXPECT_TRUE(p.call("apply_look", {{"clipIds", {clips[0]}}, {"look", "nope"}})["result"]["isError"].get<bool>());

    r = p.call("set_color", {{"clipId", clips[0]}, {"values", {{"exposure", 0.35}}}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    r = p.call("copy_grade", {{"fromClipId", clips[0]}, {"toClipIds", {clips[1]}}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    p.controller.selectClip(QString::fromStdString(clips[1]));
    EXPECT_EQ(p.controller.selection().value("look").toString(), "oppenheimer");
    EXPECT_DOUBLE_EQ(p.controller.selection().value("exposure").toDouble(), 0.35);
    EXPECT_FALSE(p.controller.hasCopiedGrade());  // the user's copied grade is untouched
    EXPECT_TRUE(p.call("copy_grade", {{"fromClipId", clips[0]}, {"toClipIds", {"00000000-0000-0000-0000-000000000000"}}})["result"]["isError"].get<bool>());

    r = p.call("apply_look", {{"clipIds", {clips[0], clips[1]}}, {"look", "none"}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    p.controller.selectClip(QString::fromStdString(clips[1]));
    EXPECT_EQ(p.controller.selection().value("look").toString(), "");
}

TEST(McpUi, NodesGradePartsOfAClipThroughTools) {
    Project p;
    const auto clip = p.clip();
    // "Warm up only the person, darken the rest": two nodes in two calls.
    auto r = p.call("add_node", {{"clipId", clip}, {"select", "person"}, {"grade", {{"temperature", 0.4}, {"exposure", 0.2}}}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    const std::string person = r["result"]["structuredContent"]["nodeId"];
    EXPECT_EQ(person, "n2");
    r = p.call("add_node", {{"clipId", clip}, {"select", "circle"}, {"label", "Spot"},
                            {"window", {{"x", 0.4}, {"y", 0.5}, {"width", 0.3}, {"softness", 0.6}}},
                            {"grade", {{"exposure", -0.5}}}, {"invert", true}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    const std::string spot = r["result"]["structuredContent"]["nodeId"];
    p.controller.selectClip(QString::fromStdString(clip));
    auto nodes = p.controller.selection().value("nodes").toList();
    ASSERT_EQ(nodes.size(), 2);
    EXPECT_EQ(nodes[0].toMap().value("subject").toString(), "person");
    EXPECT_DOUBLE_EQ(nodes[0].toMap().value("temperature").toDouble(), 0.4);
    EXPECT_EQ(nodes[1].toMap().value("label").toString(), "Spot");
    EXPECT_TRUE(nodes[1].toMap().value("invert").toBool());
    EXPECT_DOUBLE_EQ(nodes[1].toMap().value("window").toMap().value("softness").toDouble(), 0.6);

    // A color key picked on the canvas, in the same call.
    r = p.call("add_node", {{"clipId", clip}, {"select", "color"}, {"pick", {{"x", 0.5}, {"y", 0.5}, {"time", 1.0}}},
                            {"grade", {{"saturation", -1.0}}}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    // A pick after the clip ends fails and changes nothing (the assistant edit rolls back).
    p.controller.selectClip(QString::fromStdString(clip));
    const auto before = p.controller.selection().value("nodes").toList().size();
    r = p.call("add_node", {{"clipId", clip}, {"select", "color"}, {"pick", {{"x", 0.5}, {"y", 0.5}, {"time", 3000.0}}}});
    EXPECT_TRUE(r["result"]["isError"].get<bool>()) << r;
    p.controller.selectClip(QString::fromStdString(clip));
    EXPECT_EQ(p.controller.selection().value("nodes").toList().size(), before);

    r = p.call("set_node", {{"clipId", clip}, {"nodeId", spot}, {"enabled", false}, {"window", {{"shape", "none"}}}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    p.controller.selectClip(QString::fromStdString(clip));
    nodes = p.controller.selection().value("nodes").toList();
    EXPECT_FALSE(nodes[1].toMap().value("enabled").toBool());
    EXPECT_EQ(nodes[1].toMap().value("window").toMap().value("shape").toString(), "");
    EXPECT_TRUE(p.call("set_node", {{"clipId", clip}, {"nodeId", "n99"}, {"enabled", false}})["result"]["isError"].get<bool>());
    EXPECT_EQ(p.call("add_node", {{"clipId", clip}, {"select", "star"}})["error"]["code"], -32602);

    r = p.call("remove_node", {{"clipId", clip}, {"nodeId", person}});
    ASSERT_FALSE(r["result"]["isError"].get<bool>()) << r;
    p.controller.selectClip(QString::fromStdString(clip));
    EXPECT_EQ(p.controller.selection().value("nodes").toList().size(), static_cast<int>(before) - 1);
}
