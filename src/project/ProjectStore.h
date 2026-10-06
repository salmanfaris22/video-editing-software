#pragma once

#include "project/Project.h"

#include <filesystem>
#include <optional>
#include <string>

namespace lectern::project {

/// Loads and saves project folders transactionally (docs/PROJECT_FORMAT.md §4).
class ProjectStore {
public:
    static constexpr const char* kProjectFile = "project.json";
    static constexpr const char* kFolderExtension = ".lectern";

    struct LoadResult {
        Project project;
        bool restoredFromBackup = false;  ///< project.json was unreadable; .bak was used
        std::string note;
    };

    /// Reads project.json; falls back to project.json.bak if it is unreadable.
    [[nodiscard]] static Result<LoadResult> load(const std::filesystem::path& projectDir);
    /// Atomic replace with a refreshed .bak. Updates nothing in `project`.
    [[nodiscard]] static Status save(const std::filesystem::path& projectDir, const Project& project);
    /// Writes autosave/project-<UTC timestamp>.json, keeping the newest `keep`.
    [[nodiscard]] static Status writeAutosave(const std::filesystem::path& projectDir, const Project& project,
                                              int keep = 10);
    [[nodiscard]] static std::optional<std::filesystem::path> newestAutosave(const std::filesystem::path& projectDir);

    /// Creates "<parent>/<title>.lectern" (unique) with the standard subfolders.
    [[nodiscard]] static Result<std::filesystem::path> createProjectFolder(const std::filesystem::path& parent,
                                                                           const std::string& title);

    struct CollectResult {
        std::filesystem::path folder;
        std::size_t filesCopied = 0;
        std::uint64_t bytesCopied = 0;
    };

    /// Copies project.json and every media / recording file referenced by the project into one folder.
    [[nodiscard]] static Result<CollectResult> collect(const std::filesystem::path& projectDir, const Project& project,
                                                       const std::filesystem::path& destinationParent,
                                                       const std::string& folderName = {});
};

}  // namespace lectern::project
