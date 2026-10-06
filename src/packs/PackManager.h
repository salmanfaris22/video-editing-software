#pragma once

#include "core/Error.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lectern::packs {

/// One downloadable pack entry from the catalog (docs/v2/FEATURE_DELIVERY.md §4).
struct PackCatalogEntry {
    std::string id;
    std::string version;
    std::uint64_t size = 0;
    std::string url;
    std::string sha256;
};

struct PackManifest {
    std::string id;
    std::string version;
    std::string kind;
    std::string title;
    std::string description;
    std::vector<std::string> files;
};

enum class PackState { NotInstalled, Installed, UpdateAvailable };

/// Local pack store under the app data directory.
class PackManager {
public:
    explicit PackManager(std::filesystem::path storeRoot);

    [[nodiscard]] std::filesystem::path storeRoot() const noexcept { return storeRoot_; }
    [[nodiscard]] std::filesystem::path packPath(const std::string& id) const;

    [[nodiscard]] static std::filesystem::path defaultStoreRoot();

    [[nodiscard]] Result<std::vector<PackCatalogEntry>> loadEmbeddedCatalog() const;
    [[nodiscard]] PackState state(const std::string& id, const std::string& catalogVersion) const;
    [[nodiscard]] Result<std::filesystem::path> require(const std::string& id);

    /// Installs the embedded dev stub for `ai.whisper.base` (no network).
    [[nodiscard]] Status installEmbeddedStub(const std::string& id);

private:
    std::filesystem::path storeRoot_;
};

inline constexpr const char* kWhisperBasePackId = "ai.whisper.base";

}  // namespace lectern::packs
