// Project media inventory and saved edit variants (podcast / multi-cut workflows).

#include "ui/ProjectController.h"

#include "project/ProjectStore.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <filesystem>

namespace lectern::ui {

namespace {

namespace fs = std::filesystem;

QString qs(const std::string& s) { return QString::fromStdString(s); }

QString mediaKindLabel(const project::MediaSource& m) {
    switch (m.kind) {
        case project::MediaKind::Video: return QStringLiteral("video");
        case project::MediaKind::Audio: return QStringLiteral("audio");
        case project::MediaKind::Image: return QStringLiteral("image");
        default: return QStringLiteral("file");
    }
}

QString roleIcon(const project::MediaRole role) {
    switch (role) {
        case project::MediaRole::Screen: return QStringLiteral("screen");
        case project::MediaRole::Camera:
        case project::MediaRole::Phone: return QStringLiteral("camera");
        case project::MediaRole::Microphone: return QStringLiteral("mic");
        case project::MediaRole::SystemAudio: return QStringLiteral("speaker");
        default: return QStringLiteral("folder");
    }
}

QString safeDirName(QString name) {
    name = name.trimmed();
    if (name.isEmpty()) name = QStringLiteral("Edit");
    for (QChar& c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) c = QLatin1Char('_');
    }
    return name;
}

void copyProjectSnapshot(const fs::path& src, const fs::path& dest) {
    fs::create_directories(dest);
    for (const auto& entry : fs::directory_iterator(src)) {
        const auto name = entry.path().filename();
        if (name == "edits" || name == "project.json.tmp" || name == "project.json.bak") continue;
        const fs::path target = dest / name;
        if (entry.is_directory()) {
            fs::copy(entry.path(), target, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        } else {
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
        }
    }
}

}  // namespace

QVariantList ProjectController::mediaAssets() const {
    QVariantList out;
    if (!project_) return out;
    for (const auto& m : project_->media) {
        const fs::path rel(m.path);
        const QString abs = qs((dir_ / m.path).string());
        out.append(QVariantMap{
            {"id", qs(m.id.toString())},
            {"name", qs(m.name.empty() ? rel.filename().string() : m.name)},
            {"path", abs},
            {"relPath", qs(m.path)},
            {"kind", mediaKindLabel(m)},
            {"icon", roleIcon(m.role)},
            {"role", QString::fromUtf8(project::toString(m.role))},
        });
    }
    return out;
}

QVariantList ProjectController::editVariants() const {
    QVariantList out;
    if (dir_.empty()) return out;
    const fs::path editsDir = dir_ / "edits";
    if (!fs::is_directory(editsDir)) return out;
    for (const auto& entry : fs::directory_iterator(editsDir)) {
        if (!entry.is_directory()) continue;
        if (!fs::exists(entry.path() / "project.json")) continue;
        const QString jsonPath = qs((entry.path() / "project.json").string());
        out.append(QVariantMap{
            {"name", qs(entry.path().filename().string())},
            {"path", qs(entry.path().string())},
            {"modified", QFileInfo(jsonPath).lastModified().toString(Qt::ISODate)},
        });
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(
                   b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    return out;
}

void ProjectController::saveEditVariant(const QString& name) {
    if (!project_ || dir_.empty()) return;
    save();
    const QString folder = safeDirName(name);
    const fs::path dest = dir_ / "edits" / folder.toStdString();
    try {
        if (fs::exists(dest)) {
            showMessage(QStringLiteral("An edit named “%1” already exists.").arg(folder));
            return;
        }
        copyProjectSnapshot(dir_, dest);
        showMessage(QStringLiteral("Saved edit “%1”.").arg(folder));
        emit projectChanged();
    } catch (const std::exception& e) {
        showMessage(QStringLiteral("Could not save edit: %1").arg(QString::fromUtf8(e.what())));
    }
}

void ProjectController::openEditVariant(const QString& dir) {
    if (dir.isEmpty()) return;
    const QFileInfo info(dir);
    if (!info.isDir() || !QFileInfo(info.filePath() + QStringLiteral("/project.json")).exists()) {
        showMessage(QStringLiteral("That folder is not a Lectern project."));
        return;
    }
    open(info.absoluteFilePath());
}

QVariantList ProjectController::timelineClipsFlat() const {
    QVariantList out;
    for (const QVariant& t : tracks_) {
        const QVariantMap track = t.toMap();
        const QString trackName = track.value(QStringLiteral("name")).toString();
        const QString trackId = track.value(QStringLiteral("id")).toString();
        const QString trackLabel = track.value(QStringLiteral("label")).toString();
        for (const QVariant& c : track.value(QStringLiteral("clips")).toList()) {
            const QVariantMap clip = c.toMap();
            QVariantMap row = clip;
            row.insert(QStringLiteral("trackId"), trackId);
            row.insert(QStringLiteral("trackName"), trackName);
            row.insert(QStringLiteral("trackLabel"), trackLabel);
            out.append(row);
        }
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("start")).toDouble() <
               b.toMap().value(QStringLiteral("start")).toDouble();
    });
    return out;
}

}  // namespace lectern::ui
