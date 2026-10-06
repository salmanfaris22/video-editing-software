#pragma once

#include "core/Error.h"

#include <filesystem>
#include <string>
#include <vector>

namespace lectern::capture {

/// Registry of recordings that are (or were) in progress, stored as one small
/// JSON file per session in <app data>/recordings/active. An entry exists from
/// start until the session is finalized or recovered; together with the
/// session's OS file lock it identifies crashed sessions on the next launch
/// (docs/RECORDING_ENGINE.md §8).
class ActiveSessionRegistry {
public:
    struct Entry {
        std::string sessionId;
        std::filesystem::path projectDir;
        std::filesystem::path sessionDir;  ///< <project>/recordings/<id>
        std::string startedAt;
    };

    explicit ActiveSessionRegistry(std::filesystem::path directory) : dir_(std::move(directory)) {}

    Status add(const Entry& entry) const;
    Status remove(const std::string& sessionId) const;
    [[nodiscard]] Result<std::vector<Entry>> list() const;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return dir_; }

    [[nodiscard]] static std::filesystem::path defaultDirectory();

private:
    std::filesystem::path dir_;
};

}  // namespace lectern::capture
