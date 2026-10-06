#include "project/ProjectStore.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"
#include "project/ProjectJson.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <vector>

namespace lectern::project {

namespace {

Result<Project> loadFile(const std::filesystem::path& file) {
    auto text = fs::readFile(file, 256u << 20);
    if (!text) return fail(std::move(text).error());
    auto parsed = json::parse(*text, file.string());
    if (!parsed) return fail(std::move(parsed).error());
    return projectFromJson(*parsed);
}

std::string timestampForFileName() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

}  // namespace

Result<ProjectStore::LoadResult> ProjectStore::load(const std::filesystem::path& projectDir) {
    const auto main = projectDir / kProjectFile;
    auto primary = loadFile(main);
    if (primary) return LoadResult{std::move(*primary), false, {}};
    // A newer-format file must not be "repaired" from a backup.
    if (primary.error().code() == ErrorCode::Unsupported) return fail(std::move(primary).error());

    const auto backup = std::filesystem::path(main.string() + ".bak");
    auto fallback = loadFile(backup);
    if (fallback) {
        LEC_WARN("project", "project.json unreadable ({}); restored from backup", primary.error().toString());
        return LoadResult{std::move(*fallback), true,
                          "The project file was damaged; the previous saved version was restored."};
    }
    return fail(std::move(primary).error());
}

Status ProjectStore::save(const std::filesystem::path& projectDir, const Project& project) {
    LEC_TRY(project.validate());
    return fs::writeFileAtomic(projectDir / kProjectFile, json::dump(toJson(project)),
                               {.fsync = true, .keepBackup = true});
}

Status ProjectStore::writeAutosave(const std::filesystem::path& projectDir, const Project& project, int keep) {
    const auto dir = projectDir / "autosave";
    LEC_TRY(fs::ensureDirectory(dir));
    LEC_TRY(fs::writeFileAtomic(dir / ("project-" + timestampForFileName() + ".json"), json::dump(toJson(project)),
                                {.fsync = false, .keepBackup = false}));
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
        if (de.path().extension() == ".json") files.push_back(de.path());
    }
    std::sort(files.begin(), files.end());  // timestamped names sort chronologically
    while (static_cast<int>(files.size()) > keep) {
        std::filesystem::remove(files.front(), ec);
        files.erase(files.begin());
    }
    return ok();
}

std::optional<std::filesystem::path> ProjectStore::newestAutosave(const std::filesystem::path& projectDir) {
    std::optional<std::filesystem::path> newest;
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(projectDir / "autosave", ec)) {
        if (de.path().extension() != ".json") continue;
        if (!newest || de.path().filename() > newest->filename()) newest = de.path();
    }
    return newest;
}

Result<std::filesystem::path> ProjectStore::createProjectFolder(const std::filesystem::path& parent,
                                                                const std::string& title) {
    const auto dir = fs::uniquePath(parent / (fs::sanitizeFileName(title) + kFolderExtension));
    for (const char* sub : {"media", "proxies", "thumbnails", "waveforms", "cache", "autosave", "recordings"}) {
        LEC_TRY(fs::ensureDirectory(dir / sub));
    }
    return dir;
}

}  // namespace lectern::project
