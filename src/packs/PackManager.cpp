#include "packs/PackManager.h"

#include "core/FileSystem.h"
#include "core/Json.h"

#include <cstdlib>

namespace lectern::packs {

namespace {

json::Json embeddedCatalog() {
    return json::Json{{"schema", 1},
                      {"packs", json::Json::array({json::Json{{"id", kWhisperBasePackId},
                                                              {"version", "1.0.0"},
                                                              {"size", 128},
                                                              {"url", "embedded://ai.whisper.base"},
                                                              {"sha256", "dev-stub"}}})}};
}

}  // namespace

PackManager::PackManager(std::filesystem::path storeRoot) : storeRoot_(std::move(storeRoot)) {}

std::filesystem::path PackManager::defaultStoreRoot() {
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) return std::filesystem::path(home) / "Library/Application Support/Lectern/packs";
#endif
    return std::filesystem::temp_directory_path() / "lectern-packs";
}

std::filesystem::path PackManager::packPath(const std::string& id) const { return storeRoot_ / id; }

Result<std::vector<PackCatalogEntry>> PackManager::loadEmbeddedCatalog() const {
    std::vector<PackCatalogEntry> out;
    const json::Json cat = embeddedCatalog();
    if (!cat.contains("packs") || !cat["packs"].is_array()) return out;
    for (const auto& p : cat["packs"]) {
        PackCatalogEntry e;
        e.id = p.value("id", "");
        e.version = p.value("version", "");
        e.size = p.value("size", std::uint64_t{0});
        e.url = p.value("url", "");
        e.sha256 = p.value("sha256", "");
        if (!e.id.empty()) out.push_back(std::move(e));
    }
    return out;
}

PackState PackManager::state(const std::string& id, const std::string& catalogVersion) const {
    const std::filesystem::path current = packPath(id) / "current";
    std::error_code ec;
    if (!std::filesystem::exists(current, ec)) return PackState::NotInstalled;
    auto text = fs::readFile(current, 4096);
    if (!text) return PackState::NotInstalled;
    const std::string installed = *text;
    if (installed == catalogVersion) return PackState::Installed;
    return PackState::UpdateAvailable;
}

Result<std::filesystem::path> PackManager::require(const std::string& id) {
    const auto cat = loadEmbeddedCatalog();
    if (!cat) return fail(std::move(cat).error());
    std::optional<PackCatalogEntry> entry;
    for (const auto& p : *cat) {
        if (p.id == id) entry = p;
    }
    if (!entry) return fail(ErrorCode::NotFound, "unknown pack " + id);
    if (state(id, entry->version) == PackState::Installed) {
        auto text = fs::readFile(packPath(id) / "current", 4096);
        if (!text) return fail(std::move(text).error());
        return packPath(id) / *text;
    }
    LEC_TRY(installEmbeddedStub(id));
    auto text = fs::readFile(packPath(id) / "current", 4096);
    if (!text) return fail(std::move(text).error());
    return packPath(id) / *text;
}

Status PackManager::installEmbeddedStub(const std::string& id) {
    if (id != kWhisperBasePackId) return fail(ErrorCode::Unsupported, "only the dev transcription stub is built in");
    const std::string version = "1.0.0";
    const std::filesystem::path root = packPath(id) / version;
    LEC_TRY(fs::ensureDirectory(root));
    const json::Json manifest{{"id", id},
                              {"version", version},
                              {"kind", "ai-model"},
                              {"title", "Transcription (fast)"},
                              {"description", "Dev stub model path for Whisper integration tests"},
                              {"files", json::Json::array({"model.stub"})}};
    LEC_TRY(fs::writeFileAtomic(root / "pack.json", json::dump(manifest), {.fsync = true, .keepBackup = false}));
    LEC_TRY(fs::writeFileAtomic(root / "model.stub", "lectern-whisper-stub-v1\n", {.fsync = true, .keepBackup = false}));
    LEC_TRY(fs::ensureDirectory(packPath(id)));
    LEC_TRY(fs::writeFileAtomic(packPath(id) / "current", version, {.fsync = true, .keepBackup = false}));
    return ok();
}

}  // namespace lectern::packs
