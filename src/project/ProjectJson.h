#pragma once

#include "core/Json.h"
#include "project/Project.h"

namespace lectern::project {

/// Serializes the project as `project.json` (docs/PROJECT_FORMAT.md §2).
[[nodiscard]] json::Json toJson(const Project& project);

/// Parses and validates. Runs format migrations first; refuses files from a
/// newer format version instead of partially loading them.
[[nodiscard]] Result<Project> projectFromJson(const json::Json& value);

/// JSON→JSON migration to the current format version (exposed for tests).
[[nodiscard]] Result<json::Json> migrateProjectJson(json::Json value);

}  // namespace lectern::project
