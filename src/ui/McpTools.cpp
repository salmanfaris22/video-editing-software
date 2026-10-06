#include "ui/McpController.h"
#include "ui/ProjectController.h"

#include "editor/Exporter.h"
#include "editor/FrameProvider.h"
#include "editor/FrameRenderer.h"
#include "editor/RenderPlan.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace lectern::ui {
namespace {
using mcp::Json;
using mcp::Server;
using mcp::Access;
using mcp::Context;

QString str(const Json& a, const char* key, const char* fallback = "") { return QString::fromStdString(a.value(key, std::string(fallback))); }
Json json(const QVariant& v) { return Json::parse(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact).toStdString()); }
QVariant variant(const Json& v) { return QJsonDocument::fromJson(QByteArray::fromStdString(Json{{"v", v}}.dump())).object().value(QStringLiteral("v")).toVariant(); }
Json object(Json props = Json::object(), Json required = Json::array()) {
    return {{"type", "object"}, {"properties", props}, {"required", required}, {"additionalProperties", false}};
}
Json text(int max = 1000) { return {{"type", "string"}, {"maxLength", max}, {"minLength", 1}}; }
Json number(double lo, double hi) { return {{"type", "number"}, {"minimum", lo}, {"maximum", hi}}; }
Json integer(int lo, int hi) { return {{"type", "integer"}, {"minimum", lo}, {"maximum", hi}}; }
Json choice(Json values) { return {{"type", "string"}, {"enum", values}}; }
Json boolean() { return {{"type", "boolean"}}; }
Json time() { return number(0, 604800); }
Json ids() { return {{"type", "array"}, {"items", text(64)}, {"minItems", 1}, {"maxItems", 100}}; }

Json summary(ProjectController* p) {
    return {{"loaded", p->loaded()}, {"title", p->title().toStdString()}, {"duration", p->duration()},
            {"canvas", {{"width", p->canvasWidth()}, {"height", p->canvasHeight()}, {"fps", p->frameRate()}}},
            {"canUndo", p->canUndo()}, {"canRedo", p->canRedo()}, {"undoLabel", p->undoLabel().toStdString()}};
}

Json timeline(ProjectController* p) {
    auto data = summary(p);
    data["tracks"] = json(p->tracks());
    data["markers"] = json(p->markers());
    data["layoutRegions"] = json(p->layoutRegions());
    return data;
}

void requireClip(ProjectController* p, const QString& id) {
    const auto parsed = Uuid::parse(id.toStdString());
    if (!parsed || !p->snapshot()->timeline.findClip(timeline::ClipId(*parsed))) throw std::runtime_error("Clip not found");
}

void requireTrack(ProjectController* p, const QString& id) {
    const auto parsed = Uuid::parse(id.toStdString());
    if (!parsed || !p->snapshot()->timeline.findTrack(timeline::TrackId(*parsed))) throw std::runtime_error("Layer not found");
}


/// Renders frames for render_frame (same renderer as the preview; created lazily, reused).
struct FrameRendering {
    std::filesystem::path dir;
    std::unique_ptr<editor::FrameProvider> frames;
    std::unique_ptr<editor::FrameRenderer> renderer;
};

/// Background exports started by export_video. Joined (after cancelling) when the tools go away.
struct ExportJobs {
    struct Job {
        std::string id;
        std::string output;
        std::atomic<double> fraction{0};
        std::atomic<bool> cancel{false};
        std::atomic<bool> done{false};
        std::mutex mutex;
        std::string error;  ///< empty while running / on success
        double seconds = 0;
    };
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<Job>> jobs;
    std::vector<std::thread> threads;
    int next = 1;
    ~ExportJobs() {
        for (auto& [id, job] : jobs) job->cancel = true;
        for (auto& t : threads) if (t.joinable()) t.join();
    }
};

/// A plain file name for the project's Exports folder (no folders, no tricks).
QString exportFileName(const QString& name, const QString& extension) {
    QString base = QFileInfo(name).completeBaseName();
    for (QChar& c : base) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('-') || c == QLatin1Char('_'))) c = QLatin1Char('_');
    }
    base = base.trimmed().left(80);
    if (base.isEmpty()) throw std::runtime_error("Give the file a name");
    return base + extension;
}

/// A path inside the open project's folder (subtitle imports).
QString projectFile(ProjectController* p, const QString& path) {
    const QDir dir(QString::fromStdString(p->directory().string()));
    const QString full = QDir::cleanPath(QFileInfo(path).isAbsolute() ? path : dir.filePath(path));
    const QString root = QDir::cleanPath(dir.absolutePath()) + QLatin1Char('/');
    if (!full.startsWith(root)) throw std::runtime_error("Only files inside the project folder can be used");
    if (!QFileInfo(full).isFile()) throw std::runtime_error("File not found in the project folder");
    return full;
}

// Resource and tool callbacks only operate on the already-open project.
void requireProject(ProjectController* p) {
    if (!p->loaded() || p->loading()) throw std::runtime_error("Open a project in Lectern first");
}
} // namespace

void registerMcpTools(Server& server, ProjectController* p) {
    using Action = std::function<Json(const Json&, const Context&)>;
    const auto add = [&](std::string name, std::string description, Json schema, Access access, Action action, bool destructive = false) {
        server.addTool({std::move(name), std::move(description), std::move(schema), access, destructive, access == Access::Read,
                        [p, action = std::move(action)](const Json& a, const Context& ctx) {
                            requireProject(p);
                            for (const auto* key : {"clipId"}) if (a.contains(key)) requireClip(p, str(a, key));
                            for (const auto* key : {"layerId", "trackId"}) if (a.contains(key)) requireTrack(p, str(a, key));
                            if (a.contains("clipIds")) for (const auto& id : a["clipIds"]) requireClip(p, QString::fromStdString(id.get<std::string>()));
                            return action(a, ctx);
                        }});
    };
    const auto edit = [&](std::string name, std::string description, Json schema,
                          std::function<void(const Json&)> action, bool destructive = false) {
        const auto label = QString::fromStdString(name);
        add(std::move(name), std::move(description), std::move(schema), Access::Edit,
            [p, action = std::move(action), label](const Json& a, const Context& ctx) {
                const auto error = p->assistantEdit(QString::fromStdString(ctx.session.id), label, [&] { action(a); });
                return error.isEmpty() ? Server::result(summary(p)) : Server::failure(error.toStdString());
            }, destructive);
    };
    add("get_project", "Read the open project's summary and undo state.", object(), Access::Read,
        [p](const Json&, const Context&) { return Server::result(summary(p)); });
    add("get_timeline", "Read all tracks, clips, markers and layout regions. Times are seconds.", object(), Access::Read,
        [p](const Json&, const Context&) { return Server::result(timeline(p)); });
    add("get_selection", "Read the current clip and layer selection.", object(), Access::Read,
        [p](const Json&, const Context&) { return Server::result(Json{{"clipIds", json(QVariant(p->selectedClips()))}, {"layerId", p->selectedTrack().toStdString()}, {"selection", json(p->selection())}}); });
    add("list_presets", "Read the installed layout presets and built-in LUT identifiers.", object(), Access::Read,
        [p](const Json&, const Context&) { return Server::result(Json{{"layouts", json(p->layoutPresets())}, {"luts", json(p->builtinLuts())}}); });

    edit("split_at", "Split a clip at a timeline time, or all recording tracks when clipId is omitted.",
         object({{"time", time()}, {"clipId", text(64)}}, {"time"}), [p](const Json& a) {
             const auto t = a["time"].get<double>();
             if (a.contains("clipId")) {
                 const auto id = Uuid::parse(str(a, "clipId").toStdString());
                 const auto* clip = p->snapshot()->timeline.findClip(timeline::ClipId(*id));
                 if (t <= clip->range.start.toSecondsF() || t >= clip->range.end().toSecondsF()) throw std::runtime_error("Split time must be inside the clip");
                 p->selectClip(str(a, "clipId"));
             } else p->clearSelection();
             p->splitAt(t);
         });
    edit("delete_clips", "Delete clips with a gap, or ripple linked recording segments on every track.",
         object({{"clipIds", ids()}, {"mode", choice({"gap", "ripple"})}}, {"clipIds", "mode"}), [p](const Json& a) {
             p->setLinkedEditMode(a["mode"] == "ripple" ? QStringLiteral("allTracks") : QStringLiteral("track"));
             QStringList list;
             for (const auto& id : a["clipIds"]) list.append(QString::fromStdString(id.get<std::string>()));
             p->selectClips(list); p->deleteSelected();
         }, true);
    edit("remove_range", "Ripple-remove a range of at least 40 ms from all unlocked tracks.",
         object({{"start", time()}, {"end", time()}}, {"start", "end"}), [p](const Json& a) {
             const double start = a["start"], end = a["end"];
             if (end - start < 0.04 || end > p->duration()) throw std::runtime_error("Range must be within the timeline and at least 40 ms long");
             p->removeRange(start, end);
         }, true);
    edit("trim_clip", "Trim a clip edge to an absolute timeline time.",
         object({{"clipId", text(64)}, {"edge", choice({"start", "end"})}, {"time", time()}}, {"clipId", "edge", "time"}),
         [p](const Json& a) { p->trimClip(str(a, "clipId"), str(a, "edge"), a["time"]); });
    edit("move_clip", "Move a clip to a timeline time, optionally to a compatible layer.",
         object({{"clipId", text(64)}, {"time", time()}, {"layerId", text(64)}}, {"clipId", "time"}),
         [p](const Json& a) {
             if (a.contains("layerId")) p->moveClipToTrack(str(a, "clipId"), str(a, "layerId"), a["time"]);
             else p->moveClip(str(a, "clipId"), a["time"]);
         });
    edit("add_marker", "Add a labelled timeline marker.", object({{"time", time()}, {"label", text()}}, {"time", "label"}),
         [p](const Json& a) { p->addMarker(a["time"], str(a, "label")); });
    edit("remove_marker", "Remove a timeline marker by id.", object({{"markerId", text(64)}}, {"markerId"}),
         [p](const Json& a) { p->removeMarker(str(a, "markerId")); }, true);
    edit("set_layout", "Apply an installed layout to the whole timeline or from a given time. Use list_presets for identifiers.",
         object({{"preset", text(64)}, {"from", time()}}, {"preset"}), [p](const Json& a) {
             bool found = false;
             for (const auto& value : p->layoutPresets()) if (value.toMap().value(QStringLiteral("id")).toString() == str(a, "preset")) found = true;
             if (!found) throw std::runtime_error("Unknown layout preset");
             if (a.contains("from")) p->setLayoutFrom(str(a, "preset"), a["from"]);
             else p->setLayoutPreset(str(a, "preset"));
         });
    edit("add_layer", "Add an empty overlay, audio or subtitle layer; returns it as the selected layer.",
         object({{"kind", choice({"overlay", "audio", "subtitle"})}}, {"kind"}), [p](const Json& a) { p->addTrack(str(a, "kind")); });
    edit("rename_layer", "Rename an existing layer.", object({{"layerId", text(64)}, {"name", text(128)}}, {"layerId", "name"}),
         [p](const Json& a) {
             if (str(a, "name").trimmed().isEmpty()) throw std::runtime_error("Layer name cannot be blank");
             p->renameTrack(str(a, "layerId"), str(a, "name"));
         });
    edit("move_layer", "Move a layer one position up (1) or down (-1) within its kind.",
         object({{"layerId", text(64)}, {"direction", {{"type", "integer"}, {"enum", {-1, 1}}}}}, {"layerId", "direction"}),
         [p](const Json& a) { p->moveTrack(str(a, "layerId"), a["direction"]); });
    edit("delete_layer", "Delete an empty layer. Layers containing clips must be deleted by the user in Lectern.",
         object({{"layerId", text(64)}}, {"layerId"}), [p](const Json& a) {
             const auto id = Uuid::parse(str(a, "layerId").toStdString());
             if (!p->snapshot()->timeline.findTrack(timeline::TrackId(*id))->clips.empty()) throw std::runtime_error("Deleting a populated layer requires confirmation in Lectern; use the app's layer controls");
             p->deleteTrack(str(a, "layerId"));
         }, true);
    edit("add_text", "Add a text overlay. Its id becomes the selected clip.",
         object({{"text", text(10000)}, {"time", time()}, {"duration", number(0.04, 3600)}, {"preset", choice({"title", "lower-third", "caption", "callout"})}}, {"text", "time", "duration"}),
         [p](const Json& a) {
             if (str(a, "text").trimmed().isEmpty()) throw std::runtime_error("Text cannot be blank");
             p->addText(str(a, "text"), a["time"], a["duration"], str(a, "preset", "title"));
         });
    edit("set_text", "Replace a text overlay's content.", object({{"clipId", text(64)}, {"text", text(10000)}}, {"clipId", "text"}),
         [p](const Json& a) { p->setText(str(a, "clipId"), str(a, "text")); });
    edit("add_subtitle", "Add one timed subtitle. Automatic transcription is not yet available.",
         object({{"text", text(10000)}, {"time", time()}, {"duration", number(0.04, 3600)}}, {"text", "time", "duration"}),
         [p](const Json& a) {
             if (str(a, "text").trimmed().isEmpty()) throw std::runtime_error("Subtitle cannot be blank");
             p->addSubtitle(str(a, "text"), a["time"], a["duration"]);
         });
    edit("set_color", "Patch current basic color controls without resetting omitted controls. Exposure is -2..2; other controls -1..1.",
         object({{"clipId", text(64)}, {"values", object({{"exposure", number(-2, 2)}, {"brightness", number(-1, 1)},
                    {"contrast", number(-1, 1)}, {"saturation", number(-1, 1)}, {"temperature", number(-1, 1)}, {"tint", number(-1, 1)},
                    {"pivot", number(0, 1)}, {"shadows", number(-1, 1)}, {"highlights", number(-1, 1)},
                    {"colorBoost", number(-1, 1)}, {"hue", number(-1, 1)}})}}, {"clipId", "values"}),
         [p](const Json& a) { for (const auto& [key, value] : a["values"].items()) p->setColorValue(str(a, "clipId"), QString::fromStdString(key), value); });
    edit("set_color_wheel", "Adjust one current color wheel with normalized coordinates and master.",
         object({{"clipId", text(64)}, {"wheel", choice({"lift", "gamma", "gain", "offset"})}, {"x", number(-1, 1)}, {"y", number(-1, 1)}, {"master", number(-1, 1)}},
                {"clipId", "wheel", "x", "y", "master"}),
         [p](const Json& a) { p->setColorWheel(str(a, "clipId"), str(a, "wheel"), a["x"], a["y"], a["master"]); });
    edit("set_curve",
         "Set a custom curve (Resolve Curves - Custom): channel y (luma, all channels), r, g or b; points are "
         "[[in, out], ...] in 0..1 (at least 2; fewer removes the curve).",
         object({{"clipId", text(64)}, {"channel", choice({"y", "r", "g", "b"})},
                 {"points", {{"type", "array"}, {"maxItems", 32},
                             {"items", {{"type", "array"}, {"minItems", 2}, {"maxItems", 2}, {"items", number(0, 1)}}}}}},
                {"clipId", "channel", "points"}),
         [p](const Json& a) {
             QVariantList pts;
             // One QVariant per point (QList::append(QList) would concatenate the numbers).
             for (const auto& pt : a["points"]) pts.append(QVariant(QVariantList{pt[0].get<double>(), pt[1].get<double>()}));
             p->setCurve(str(a, "clipId"), str(a, "channel"), pts);
         });
    edit("set_hsl_curve",
         "Set an HSL curve (Resolve Curves - Hue vs Hue / Hue vs Sat / Hue vs Lum / Lum vs Sat / Sat vs Sat / Sat vs Lum) "
         "on a clip's correction, or on a node with nodeId. points are [[x, y], ...] in 0..1 where y 0.5 = no change: "
         "hue shift (y 0..1 = -180..+180 degrees), saturation gain (y 0..1 = x0..x2) or luminance (y 0..1 = -0.5..+0.5). "
         "x is hue (0 red, 0.333 green, 0.667 blue; hue curves wrap), luminance or saturation. Fewer than 2 points removes it. "
         "Example, desaturate greens: curve hueVsSat, points [[0.2,0.5],[0.333,0.15],[0.46,0.5]].",
         object({{"clipId", text(64)}, {"nodeId", text(16)},
                 {"curve", choice({"hueVsHue", "hueVsSat", "hueVsLum", "lumVsSat", "satVsSat", "satVsLum"})},
                 {"points", {{"type", "array"}, {"maxItems", 32},
                             {"items", {{"type", "array"}, {"minItems", 2}, {"maxItems", 2}, {"items", number(0, 1)}}}}}},
                {"clipId", "curve", "points"}),
         [p](const Json& a) {
             QVariantList pts;
             for (const auto& pt : a["points"]) pts.append(QVariant(QVariantList{pt[0].get<double>(), pt[1].get<double>()}));
             if (a.contains("nodeId")) {
                 bool found = false;
                 p->selectClip(str(a, "clipId"));
                 for (const QVariant& n : p->selection().value(QStringLiteral("nodes")).toList()) found = found || n.toMap().value("id").toString() == str(a, "nodeId");
                 if (!found) throw std::runtime_error("Unknown nodeId for this clip");
                 p->setNodeHslCurve(str(a, "clipId"), str(a, "nodeId"), str(a, "curve"), pts);
             } else {
                 p->setHslCurve(str(a, "clipId"), str(a, "curve"), pts);
             }
         });
    edit("reset_color", "Reset a clip's current color adjustments.", object({{"clipId", text(64)}}, {"clipId"}),
         [p](const Json& a) { p->resetColor(str(a, "clipId")); });
    edit("apply_lut", "Apply a built-in LUT from list_presets. File-based LUT imports are not exposed.",
         object({{"clipId", text(64)}, {"lut", text(128)}, {"amount", number(0, 1)}}, {"clipId", "lut"}),
         [p](const Json& a) {
             if (!str(a, "lut").startsWith(QStringLiteral("builtin:"))) throw std::runtime_error("Only built-in LUTs are available through this tool");
             p->setColorLut(str(a, "clipId"), str(a, "lut"));
             if (a.contains("amount")) p->setColorValue(str(a, "clipId"), QStringLiteral("lutAmount"), a["amount"]);
         });
    add("list_looks",
        "Read the cinematic looks (built-in and the user's saved looks): id, name, category, description. "
        "Apply one with apply_look; a look sits on top of the clip's own correction.",
        object(), Access::Read, [p](const Json&, const Context&) { return Server::result(Json{{"looks", json(p->looks())}}); });
    edit("apply_look",
         "Put a cinematic look (from list_looks, e.g. oppenheimer, dark-green, teal-orange) on top of the clips' "
         "correction at amount 0..1 (1 = full look). look \"none\" removes the look.",
         object({{"clipIds", ids()}, {"look", text(64)}, {"amount", number(0, 1)}}, {"clipIds", "look"}),
         [p](const Json& a) {
             const QString look = str(a, "look");
             bool known = look == QLatin1String("none");
             for (const QVariant& v : p->looks()) known = known || v.toMap().value("id").toString() == look;
             if (!known) throw std::runtime_error("Unknown look; use list_looks for identifiers");
             const double amount = a.value("amount", 1.0);
             for (const auto& id : a["clipIds"]) p->applyLook(QString::fromStdString(id.get<std::string>()), look, amount);
         });
    edit("copy_grade",
         "Copy one clip's whole grade (correction, curves, LUT and look) onto other clips, like copying a grade "
         "between shots in DaVinci Resolve. The target clips keep how their source colors are read.",
         object({{"fromClipId", text(64)}, {"toClipIds", ids()}}, {"fromClipId", "toClipIds"}),
         [p](const Json& a) {
             requireClip(p, str(a, "fromClipId"));
             QStringList targets;
             for (const auto& id : a["toClipIds"]) {
                 targets << QString::fromStdString(id.get<std::string>());
                 requireClip(p, targets.back());
             }
             p->copyGradeTo(str(a, "fromClipId"), targets);
         });
    const Json gradeSchema = object({{"exposure", number(-2, 2)},    {"brightness", number(-1, 1)}, {"contrast", number(-1, 1)},
                                     {"pivot", number(0, 1)},       {"saturation", number(-1, 1)}, {"temperature", number(-1, 1)},
                                     {"tint", number(-1, 1)},       {"shadows", number(-1, 1)},    {"highlights", number(-1, 1)},
                                     {"colorBoost", number(-1, 1)}, {"hue", number(-1, 1)}});
    const Json windowSchema = object({{"shape", choice({"none", "circle", "rectangle", "gradient"})}, {"x", number(-1, 2)},
                                      {"y", number(-1, 2)}, {"width", number(0.002, 4)}, {"height", number(0.002, 4)},
                                      {"rotation", number(-360, 360)}, {"softness", number(0, 1)}, {"invert", boolean()}});
    const Json qualifierSchema = object({{"enabled", boolean()}, {"hue", number(0, 1)}, {"hueWidth", number(0, 0.5)},
                                         {"satLow", number(0, 1)}, {"satHigh", number(0, 1)}, {"lumLow", number(0, 1)},
                                         {"lumHigh", number(0, 1)}, {"invert", boolean()}});
    const Json pickSchema = object({{"x", number(0, 1)}, {"y", number(0, 1)}, {"time", time()}}, {"x", "y", "time"});
    // Shared by add_node and set_node: everything after the node exists.
    const auto configureNode = [p](const Json& a, const QString& clip, const QString& nodeId) {
        if (a.contains("label")) p->setNodeLabel(clip, nodeId, str(a, "label"));
        if (a.contains("enabled")) p->setNodeEnabled(clip, nodeId, a["enabled"].get<bool>());
        if (a.contains("subject")) p->setNodeSubject(clip, nodeId, str(a, "subject") == QLatin1String("none") ? QString() : str(a, "subject"));
        if (a.contains("window")) {
            QVariantMap m = variant(a["window"]).toMap();
            if (m.value(QStringLiteral("shape")).toString() == QLatin1String("none")) m[QStringLiteral("shape")] = QString();
            p->setNodeWindow(clip, nodeId, m);
        }
        if (a.contains("qualifier")) {
            QVariantMap m = variant(a["qualifier"]).toMap();
            if (!m.contains(QStringLiteral("enabled"))) m[QStringLiteral("enabled")] = true;
            p->setNodeQualifier(clip, nodeId, m);
        }
        if (a.contains("pick")) {
            const Json& k = a["pick"];
            if (!p->pickNodeColor(clip, nodeId, k["x"].get<double>(), k["y"].get<double>(), k["time"].get<double>()))
                throw std::runtime_error("The pick point is not on this clip's picture at that time");
        }
        if (a.contains("grade")) {
            for (const auto& [key, value] : a["grade"].items()) p->setNodeValue(clip, nodeId, QString::fromStdString(key), value.get<double>());
        }
        if (a.contains("invert")) p->setNodeInvert(clip, nodeId, a["invert"].get<bool>());
    };
    add("add_node",
        "Grade only part of a clip, like a serial node in DaVinci Resolve after the clip's correction. select: whole, "
        "person or background (AI person segmentation), circle, rectangle or gradient (power windows in source "
        "coordinates 0..1; shape them with window), or color (a key on one color: give pick {x, y, time} with a canvas "
        "point 0..1 on the object, e.g. the pen, or qualifier ranges). grade holds the node's own adjustments. Limits "
        "multiply (e.g. a circle and a color key). invert grades everything except the selection. Returns nodeId.",
        object({{"clipId", text(64)}, {"select", choice({"whole", "person", "background", "circle", "rectangle", "gradient", "color"})},
                {"label", text(40)}, {"window", windowSchema}, {"qualifier", qualifierSchema}, {"pick", pickSchema},
                {"grade", gradeSchema}, {"invert", boolean()}},
               {"clipId", "select"}),
        Access::Edit, [p, configureNode](const Json& a, const Context& ctx) {
            QString nodeId;
            const auto error = p->assistantEdit(QString::fromStdString(ctx.session.id), QStringLiteral("add_node"), [&] {
                const QString clip = str(a, "clipId");
                const QString select = str(a, "select");
                nodeId = p->addNode(clip, select == QLatin1String("whole") ? QString() : select);
                if (nodeId.isEmpty()) throw std::runtime_error("Could not add a node (at most 8 per clip)");
                configureNode(a, clip, nodeId);
            });
            if (!error.isEmpty()) return Server::failure(error.toStdString());
            Json out = summary(p);
            out["nodeId"] = nodeId.toStdString();
            return Server::result(out);
        });
    edit("set_node",
         "Change a node from add_node (ids are also in get_selection under nodes): label, enabled, subject (none, person, "
         "background), window, qualifier, pick, grade values, invert.",
         object({{"clipId", text(64)}, {"nodeId", text(16)}, {"label", text(40)}, {"enabled", boolean()},
                 {"subject", choice({"none", "person", "background"})}, {"window", windowSchema}, {"qualifier", qualifierSchema},
                 {"pick", pickSchema}, {"grade", gradeSchema}, {"invert", boolean()}},
                {"clipId", "nodeId"}),
         [p, configureNode](const Json& a) {
             const QString clip = str(a, "clipId");
             const QString nodeId = str(a, "nodeId");
             bool found = false;
             p->selectClip(clip);
             for (const QVariant& n : p->selection().value(QStringLiteral("nodes")).toList()) found = found || n.toMap().value("id").toString() == nodeId;
             if (!found) throw std::runtime_error("Unknown nodeId for this clip");
             configureNode(a, clip, nodeId);
         });
    edit("remove_node", "Delete a node from a clip's grade.", object({{"clipId", text(64)}, {"nodeId", text(16)}}, {"clipId", "nodeId"}),
         [p](const Json& a) { p->removeNode(str(a, "clipId"), str(a, "nodeId")); });
    edit("set_clip_color_space",
         "Set how a clip's source colors are read: auto (from the file), rec709, srgb, display-p3, rec2020, "
         "rec2020-hlg or rec2020-pq (HDR sources are tone-mapped to SDR).",
         object({{"clipId", text(64)}, {"space", choice({"auto", "rec709", "srgb", "display-p3", "rec2020", "rec2020-hlg", "rec2020-pq"})}},
                {"clipId", "space"}),
         [p](const Json& a) { p->setInputColorSpace(str(a, "clipId"), str(a, "space")); });
    edit("set_effect",
         "Enable/disable a built-in effect and set its parameters. zoom: scale, x, y. blur, vignette, background-blur: "
         "amount. film-grain: amount, size (0.5..4). glow and halation (red film halo around highlights): amount, "
         "threshold (luma where it starts), radius. film-emulation (print stage after the grade): amount, stock "
         "(0 warm print, 1 cool print, 2 soft negative). denoise (spatial noise reduction that keeps edges): luma "
         "(detail and grain), chroma (color blotches), radius. sharpen (unsharp mask): amount, radius (detail size), coring (detail "
         "below it, i.e. noise, is not sharpened).",
         object({{"clipId", text(64)},
                 {"type", choice({"blur", "vignette", "zoom", "background-blur", "film-grain", "glow", "halation", "film-emulation",
                                  "denoise", "sharpen"})},
                 {"enabled", boolean()},
                 {"params", object({{"amount", number(0, 1)}, {"scale", number(1, 8)}, {"x", number(0, 1)}, {"y", number(0, 1)},
                                    {"size", number(0.5, 4)}, {"threshold", number(0, 0.98)}, {"radius", number(0, 1)},
                                    {"stock", number(0, 2)}, {"luma", number(0, 1)}, {"chroma", number(0, 1)},
                                    {"coring", number(0, 1)}})}},
                {"clipId", "type", "enabled"}),
         [p](const Json& a) {
             const auto params = a.value("params", Json::object());
             static const std::map<std::string, std::set<std::string>> kParams{
                 {"zoom", {"scale", "x", "y"}},         {"blur", {"amount"}},
                 {"vignette", {"amount"}},              {"background-blur", {"amount"}},
                 {"film-grain", {"amount", "size"}},    {"glow", {"amount", "threshold", "radius"}},
                 {"halation", {"amount", "threshold", "radius"}}, {"film-emulation", {"amount", "stock"}},
                 {"denoise", {"luma", "chroma", "radius"}},     {"sharpen", {"amount", "radius", "coring"}}};
             const auto& allowed = kParams.at(a["type"].get<std::string>());
             for (const auto& [key, value] : params.items()) {
                 (void)value;
                 if (!allowed.contains(key)) throw std::runtime_error("Parameter does not belong to this effect");
             }
             if (!a["enabled"].get<bool>() && !params.empty()) throw std::runtime_error("Cannot set parameters while disabling the effect");
             p->setEffectEnabled(str(a, "clipId"), str(a, "type"), a["enabled"]);
             for (const auto& [key, value] : params.items()) p->setEffectValue(str(a, "clipId"), str(a, "type"), QString::fromStdString(key), value);
         });
    edit("set_style", "Set composition and subtitle appearance.", object({{"values", object({
            {"screenPadding", number(0, 0.25)}, {"screenRadius", number(0, 0.08)}, {"screenShadow", number(0, 1)},
            {"cameraShape", choice({"rect", "rounded", "circle"})}, {"cameraBorder", number(0, 0.02)},
            {"cameraBorderColor", text(64)}, {"cameraMirror", boolean()}, {"subtitleSize", number(0.02, 0.1)},
            {"subtitleColor", text(64)}, {"subtitlePosition", number(0.1, 0.95)}})}}, {"values"}),
         [p](const Json& a) { for (const auto& [key, value] : a["values"].items()) p->setStyleValue(QString::fromStdString(key), variant(value)); });
    edit("set_audio", "Set clip or track gain/mute. Fades are clip-only; solo is track-only. Supply exactly one target.",
         object({{"clipId", text(64)}, {"trackId", text(64)}, {"values", object({{"gainDb", number(-60, 12)}, {"muted", boolean()},
                                                                                 {"solo", boolean()}, {"fadeIn", number(0, 3600)}, {"fadeOut", number(0, 3600)}})}}, {"values"}),
         [p](const Json& a) {
             if (a.contains("clipId") == a.contains("trackId")) throw std::runtime_error("Supply exactly one clipId or trackId");
             for (const auto& [key, value] : a["values"].items()) {
                 if (a.contains("clipId")) p->setClipAudio(str(a, "clipId"), QString::fromStdString(key), variant(value));
                 else p->setTrackValue(str(a, "trackId"), QString::fromStdString(key), variant(value));
             }
         });
    edit("set_clip_transform", "Set a clip's position, scale and opacity.",
         object({{"clipId", text(64)}, {"x", number(-2, 2)}, {"y", number(-2, 2)}, {"scale", number(0.1, 8)}, {"opacity", number(0, 1)}}, {"clipId"}),
         [p](const Json& a) {
             if (a.contains("x") != a.contains("y")) throw std::runtime_error("Supply both x and y");
             if (a.contains("x")) p->setClipPosition(str(a, "clipId"), a["x"], a["y"]);
             if (a.contains("scale")) p->setClipScale(str(a, "clipId"), a["scale"]);
             if (a.contains("opacity")) p->setClipOpacity(str(a, "clipId"), a["opacity"]);
         });
    for (const auto* name : {"undo", "redo"}) add(name, "Move through the project's undo history (including user edits).",
        object({{"steps", integer(1, 20)}}), Access::Edit, [p, redo = std::string(name) == "redo"](const Json& a, const Context&) {
            for (int i = 0; i < a.value("steps", 1); ++i) { if (redo) p->redo(); else p->undo(); }
            return Server::result(summary(p));
        }, true);
    add("undo_assistant_edits", "Undo this connection's consecutive latest edits, stopping before any user or other assistant edit.", object(), Access::Edit,
        [p](const Json&, const Context& ctx) { return Server::result(Json{{"undone", p->undoAssistantEdits(QString::fromStdString(ctx.session.id))}}); }, true);
    add("find_pauses", "Start background narration silence analysis. Poll get_pauses until running is false.",
        object({{"thresholdDb", number(-80, -5)}, {"minPause", number(0.1, 30)}, {"padding", number(0, 2)}}), Access::Read,
        [p](const Json& a, const Context&) {
            if (p->busy()) return Server::failure("Another project analysis is running");
            p->findSilences(a.value("thresholdDb", -42.0), a.value("minPause", 0.7), a.value("padding", 0.15));
            return Server::result(Json{{"running", p->busy()}});
        });
    add("get_pauses", "Read the current silence analysis results and status.", object(), Access::Read,
        [p](const Json&, const Context&) { return Server::result(Json{{"running", p->busy()}, {"pauses", json(p->silences())}, {"total", p->silenceTotal()}, {"message", p->message().toStdString()}}); });
    edit("remove_pauses", "Remove pauses found by find_pauses. Wait for analysis to finish first.", object(),
         [p](const Json&) { p->removeSilences(); }, true);


    // ---- See the picture -------------------------------------------------------
    auto rendering = std::make_shared<FrameRendering>();
    add("render_frame", "Render the edited video at a timeline time as a PNG image (what the preview and export show).",
        object({{"time", time()}, {"width", integer(160, 1920)}}, {"time"}), Access::Read,
        [p, rendering](const Json& a, const Context&) {
            const auto snapshot = p->snapshot();
            if (!rendering->frames || rendering->dir != p->directory()) {
                rendering->dir = p->directory();
                rendering->frames = std::make_unique<editor::FrameProvider>(rendering->dir, true);
                rendering->renderer = editor::makeRenderer();
                rendering->renderer->setProjectDirectory(rendering->dir);
            }
            rendering->frames->setProject(snapshot);
            const double t = std::clamp(a["time"].get<double>(), 0.0, p->duration());
            const int w = (a.value("width", 960) / 2) * 2;
            const int h = std::max(2, static_cast<int>(std::lround(static_cast<double>(w) * snapshot->canvas.height /
                                                                    std::max(1, snapshot->canvas.width)) / 2) * 2);
            QImage image(w, h, QImage::Format_RGB32);
            rendering->renderer->render(editor::buildRenderPlan(*snapshot, Time::fromSecondsF(t)), image,
                                        [&](const editor::VisualLayer& l, QSizeF box) { return rendering->frames->image(l, box); });
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            const Json info{{"time", t}, {"width", w}, {"height", h}};
            return Json{{"content", Json::array({{{"type", "image"}, {"data", png.toBase64().toStdString()}, {"mimeType", "image/png"}},
                                                 {{"type", "text"}, {"text", info.dump()}}})},
                        {"structuredContent", info},
                        {"isError", false}};
        });

    add("get_scopes",
        "Measure the edited picture at a time like video scopes: per-channel black/white levels (0-1023), "
        "average luma and saturation, and the percentage of clipped pixels.",
        object({{"time", time()}}, {"time"}), Access::Read,
        [p, rendering](const Json& a, const Context&) {
            const auto snapshot = p->snapshot();
            if (!rendering->frames || rendering->dir != p->directory()) {
                rendering->dir = p->directory();
                rendering->frames = std::make_unique<editor::FrameProvider>(rendering->dir, true);
                rendering->renderer = editor::makeRenderer();
                rendering->renderer->setProjectDirectory(rendering->dir);
            }
            rendering->frames->setProject(snapshot);
            const double t = std::clamp(a["time"].get<double>(), 0.0, p->duration());
            QImage image(320, std::max(2, 320 * snapshot->canvas.height / std::max(1, snapshot->canvas.width)), QImage::Format_RGB32);
            rendering->renderer->render(editor::buildRenderPlan(*snapshot, Time::fromSecondsF(t)), image,
                                        [&](const editor::VisualLayer& l, QSizeF box) { return rendering->frames->image(l, box); });
            std::array<int, 3> lo{255, 255, 255};
            std::array<int, 3> hi{0, 0, 0};
            double lumaSum = 0, satSum = 0;
            long long clipped = 0;
            const long long n = static_cast<long long>(image.width()) * image.height();
            for (int y = 0; y < image.height(); ++y) {
                const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    const std::array<int, 3> c{qRed(row[x]), qGreen(row[x]), qBlue(row[x])};
                    for (int i = 0; i < 3; ++i) {
                        lo[static_cast<std::size_t>(i)] = std::min(lo[static_cast<std::size_t>(i)], c[static_cast<std::size_t>(i)]);
                        hi[static_cast<std::size_t>(i)] = std::max(hi[static_cast<std::size_t>(i)], c[static_cast<std::size_t>(i)]);
                    }
                    lumaSum += 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
                    const int mx = std::max({c[0], c[1], c[2]});
                    const int mn = std::min({c[0], c[1], c[2]});
                    satSum += mx > 0 ? static_cast<double>(mx - mn) / mx : 0.0;
                    if (mx >= 254 || mn <= 1) ++clipped;
                }
            }
            auto ten = [](int v) { return v * 1023 / 255; };
            return Server::result(Json{{"time", t},
                                       {"black", {{"r", ten(lo[0])}, {"g", ten(lo[1])}, {"b", ten(lo[2])}}},
                                       {"white", {{"r", ten(hi[0])}, {"g", ten(hi[1])}, {"b", ten(hi[2])}}},
                                       {"averageLuma", lumaSum / static_cast<double>(n) * 1023.0 / 255.0},
                                       {"averageSaturation", satSum / static_cast<double>(n)},
                                       {"clippedPercent", 100.0 * static_cast<double>(clipped) / static_cast<double>(n)}});
        });

    // ---- Export -----------------------------------------------------------------
    auto jobs = std::make_shared<ExportJobs>();
    add("export_video",
        "Export the edited video (H.264 + AAC) into the project's Exports folder in the background. Poll get_export.",
        object({{"name", text(80)},
                {"resolution", choice({"720p", "1080p", "1440p", "4K"})},
                {"quality", choice({"draft", "standard", "high", "max"})},
                {"fps", integer(12, 60)},
                {"start", time()},
                {"end", time()}},
               {"name"}),
        Access::Full,
        [p, jobs](const Json& a, const Context&) {
            const auto snapshot = p->snapshot();
            if (p->duration() <= 0) throw std::runtime_error("The timeline is empty");
            const QDir exports(QDir(QString::fromStdString(p->directory().string())).filePath(QStringLiteral("Exports")));
            if (!QDir().mkpath(exports.absolutePath())) throw std::runtime_error("Cannot create the Exports folder");
            QString file = exportFileName(QString::fromStdString(a["name"].get<std::string>()), QStringLiteral(".mp4"));
            for (int i = 2; QFileInfo::exists(exports.filePath(file)); ++i) {
                file = QFileInfo(file).completeBaseName().section(QLatin1Char(' '), 0, -1) + QStringLiteral(" %1.mp4").arg(i);
            }
            // Same sizes as the export dialog: the shorter side is the resolution.
            const std::string res = a.value("resolution", std::string("1080p"));
            const int target = res == "720p" ? 720 : res == "1440p" ? 1440 : res == "4K" ? 2160 : 1080;
            const int cw = snapshot->canvas.width;
            const int ch = snapshot->canvas.height;
            const double scale = static_cast<double>(target) / std::max(2, std::min(cw, ch));
            editor::ExportOptions options;
            options.output = exports.filePath(file).toStdString();
            options.width = static_cast<int>(std::lround(cw * scale / 2.0)) * 2;
            options.height = static_cast<int>(std::lround(ch * scale / 2.0)) * 2;
            options.frameRate = FrameRate(a.value("fps", 30), 1);
            options.quality = a.value("quality", std::string("high"));
            if (a.contains("start") || a.contains("end")) {
                const double start = a.value("start", 0.0);
                const double end = std::min(a.value("end", p->duration()), p->duration());
                if (end - start < 0.1) throw std::runtime_error("The range must be at least 0.1 s long");
                options.range = TimeRange{Time::fromSecondsF(start), Time::fromSecondsF(end - start)};
            }
            auto job = std::make_shared<ExportJobs::Job>();
            {
                std::lock_guard lock(jobs->mutex);
                job->id = "export-" + std::to_string(jobs->next++);
                job->output = options.output.string();
                jobs->jobs[job->id] = job;
                jobs->threads.emplace_back([job, snapshot, dir = p->directory(), options] {
                    const auto started = std::chrono::steady_clock::now();
                    auto result = editor::exportProject(*snapshot, dir, options,
                                                        [job](const editor::ExportProgress& pr) { job->fraction = pr.fraction; },
                                                        &job->cancel);
                    std::lock_guard l(job->mutex);
                    if (!result) job->error = result.error().message();
                    job->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                    if (result) job->fraction = 1.0;
                    job->done = true;
                });
            }
            return Server::result(Json{{"jobId", job->id}, {"output", job->output}, {"width", options.width}, {"height", options.height}});
        });
    const auto findJob = [jobs](const Json& a) {
        std::lock_guard lock(jobs->mutex);
        const auto it = jobs->jobs.find(a["jobId"].get<std::string>());
        if (it == jobs->jobs.end()) throw std::runtime_error("Unknown export job");
        return it->second;
    };
    add("get_export", "Read an export job's progress (0..1), state and output file.", object({{"jobId", text(32)}}, {"jobId"}),
        Access::Read, [findJob](const Json& a, const Context&) {
            const auto job = findJob(a);
            std::lock_guard lock(job->mutex);
            const std::string state = !job->done ? "running" : job->cancel ? "cancelled" : job->error.empty() ? "done" : "failed";
            Json out{{"jobId", job->id}, {"state", state}, {"progress", job->fraction.load()}, {"output", job->output}};
            if (!job->error.empty()) out["error"] = job->error;
            if (job->done) out["seconds"] = job->seconds;
            return Server::result(out);
        });
    add("cancel_export", "Cancel a running export (the partial file is removed).", object({{"jobId", text(32)}}, {"jobId"}),
        Access::Full, [findJob](const Json& a, const Context&) {
            findJob(a)->cancel = true;
            return Server::result(Json{{"cancelled", true}});
        });

    // ---- Subtitles ------------------------------------------------------------------
    edit("import_subtitles", "Import an .srt or .vtt file from inside the project folder onto the subtitle track.",
         object({{"path", text(500)}}, {"path"}), [p](const Json& a) {
             const QString file = projectFile(p, QString::fromStdString(a["path"].get<std::string>()));
             if (!file.endsWith(QStringLiteral(".srt"), Qt::CaseInsensitive) && !file.endsWith(QStringLiteral(".vtt"), Qt::CaseInsensitive)) {
                 throw std::runtime_error("Only .srt and .vtt files can be imported");
             }
             p->importSubtitles(QUrl::fromLocalFile(file));  // problems arrive as an assistant-edit error
         });
    add("export_subtitles", "Write the subtitles as .srt or .vtt into the project's Exports folder.",
        object({{"name", text(80)}, {"format", choice({"srt", "vtt"})}}, {"name"}), Access::Full,
        [p](const Json& a, const Context&) {
            const QString ext = a.value("format", std::string("srt")) == "vtt" ? QStringLiteral(".vtt") : QStringLiteral(".srt");
            const QDir exports(QDir(QString::fromStdString(p->directory().string())).filePath(QStringLiteral("Exports")));
            if (!QDir().mkpath(exports.absolutePath())) throw std::runtime_error("Cannot create the Exports folder");
            const QString path = exports.filePath(exportFileName(QString::fromStdString(a["name"].get<std::string>()), ext));
            if (!p->exportSubtitles(QUrl::fromLocalFile(path))) throw std::runtime_error(p->message().toStdString());
            return Server::result(Json{{"output", path.toStdString()}});
        });

    server.addResource("lectern://project/current", "Current project", "Summary of the open project", [p](const Context&) { requireProject(p); return summary(p); });
    server.addResource("lectern://project/timeline", "Timeline", "Clips, tracks, markers and layout regions", [p](const Context&) { requireProject(p); return timeline(p); });
    server.addPrompt("clean_up_recording", "Review and tighten pauses in a recording",
                     "Read get_timeline. Analyze with find_pauses and poll get_pauses. Explain the proposed cuts and ask the user before calling remove_pauses. Use add_text for requested titles. Do not claim automatic transcription is available unless tools/list provides it. Use render_frame to check the result visually. Check the timeline after editing and explain undo.");
    server.addPrompt("add_chapters", "Create chapter markers from a supplied outline",
                     "Read get_timeline, ask the user for a timed outline if no transcript tool is available, and propose chapter names and times. After approval, add_marker for each chapter. Never invent a transcript.");
}


void registerMcpAppTools(Server& server, McpAppHooks hooks) {
    if (hooks.recentProjects) {
        server.addTool({"list_projects", "List recent Lectern projects (name, folder, last change).", object(), Access::Read, false, true,
                        [recent = hooks.recentProjects](const Json&, const Context&) { return Server::result(Json{{"projects", json(recent())}}); }});
    }
    if (hooks.openProject && hooks.recentProjects) {
        server.addTool({"open_project", "Open one of the recent projects in Lectern (replaces the open project).",
                        object({{"path", text(1000)}}, {"path"}), Access::Full, false, false,
                        [hooks](const Json& a, const Context&) {
                            const QString path = QString::fromStdString(a["path"].get<std::string>());
                            bool known = false;
                            for (const QVariant& v : hooks.recentProjects()) known = known || v.toMap().value("path").toString() == path;
                            if (!known) throw std::runtime_error("Only projects from list_projects can be opened");
                            hooks.openProject(path);
                            return Server::result(Json{{"opening", path.toStdString()}});
                        }});
    }
}

} // namespace lectern::ui
