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

/// A clip's whole grade (correction, curves, LUT, look, nodes): gallery stills store it.
[[nodiscard]] json::Json toJson(const ColorAdjustments& color);
[[nodiscard]] Result<ColorAdjustments> colorAdjustmentsFromJson(const json::Json& value, const std::string& path);

/// A clip's creative look (also the format of saved looks). Reading is
/// lenient: unknown or out-of-range values fall back to neutral.
[[nodiscard]] json::Json toJson(const ColorAdjustments::Look& look);
[[nodiscard]] ColorAdjustments::Look lookFromJson(const json::Json& value);

}  // namespace lectern::timeline
