#include "capture/ActiveSessionRegistry.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"

namespace lectern::capture {

std::filesystem::path ActiveSessionRegistry::defaultDirectory() {
    return fs::appDataDirectory() / "recordings" / "active";
}

Status ActiveSessionRegistry::add(const Entry& entry) const {
    const json::Json j{{"sessionId", entry.sessionId},
                       {"projectDir", entry.projectDir.string()},
                       {"sessionDir", entry.sessionDir.string()},
                       {"startedAt", entry.startedAt}};
    return fs::writeFileAtomic(dir_ / (entry.sessionId + ".json"), json::dump(j));
}

Status ActiveSessionRegistry::remove(const std::string& sessionId) const {
    std::error_code ec;
    std::filesystem::remove(dir_ / (sessionId + ".json"), ec);
    if (ec) return fail(ErrorCode::IoError, "remove registry entry " + sessionId + ": " + ec.message());
    return ok();
}

Result<std::vector<ActiveSessionRegistry::Entry>> ActiveSessionRegistry::list() const {
    std::vector<Entry> out;
    std::error_code ec;
    if (!std::filesystem::exists(dir_, ec)) return out;
    for (const auto& de : std::filesystem::directory_iterator(dir_, ec)) {
        if (!de.is_regular_file() || de.path().extension() != ".json") continue;
        auto text = fs::readFile(de.path(), 64u << 10);
        if (!text) continue;
        auto parsed = json::parse(*text, de.path().string());
        if (!parsed || !parsed->is_object()) {
            LEC_WARN("recovery", "ignoring unreadable registry entry {}", de.path().string());
            continue;
        }
        Entry e;
        e.sessionId = parsed->value("sessionId", "");
        e.projectDir = parsed->value("projectDir", "");
        e.sessionDir = parsed->value("sessionDir", "");
        e.startedAt = parsed->value("startedAt", "");
        if (!e.sessionId.empty() && !e.sessionDir.empty()) out.push_back(std::move(e));
    }
    if (ec) return fail(ErrorCode::IoError, "list registry " + dir_.string() + ": " + ec.message());
    return out;
}

}  // namespace lectern::capture
