#pragma once

#include "core/Json.h"
#include "timeline/Timeline.h"

namespace lectern::timeline {

// JSON encoding of the timeline (docs/PROJECT_FORMAT.md §2). Parsing reports
// precise paths ("timeline.tracks[2].clips[5].duration: ...").

[[nodiscard]] json::Json toJson(const Timeline& timeline);
[[nodiscard]] Result<Timeline> timelineFromJson(const json::Json& value, const std::string& path = "timeline");

[[nodiscard]] json::Json toJson(const Clip& clip);
[[nodiscard]] Result<Clip> clipFromJson(const json::Json& value, const std::string& path);

}  // namespace lectern::timeline
