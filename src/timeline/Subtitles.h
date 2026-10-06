#pragma once

// Subtitle cues ⇄ SRT / WebVTT, and ⇄ the timeline's subtitle track.

#include "timeline/Timeline.h"

#include <string>
#include <string_view>
#include <vector>

namespace lectern::timeline {

struct SubtitleCue {
    TimeRange range;
    std::string text;  ///< may contain '\n'
    friend bool operator==(const SubtitleCue&, const SubtitleCue&) = default;
};

/// Parses SRT or WebVTT (detected from the "WEBVTT" header). Tolerates a BOM,
/// CRLF line endings, missing cue numbers, cue settings and inline tags
/// (which are stripped). Fails when no cue can be read.
[[nodiscard]] Result<std::vector<SubtitleCue>> parseSubtitles(std::string_view text);

[[nodiscard]] std::string writeSrt(const std::vector<SubtitleCue>& cues);
[[nodiscard]] std::string writeVtt(const std::vector<SubtitleCue>& cues);

/// Cues of all enabled subtitle clips, in time order.
[[nodiscard]] std::vector<SubtitleCue> subtitleCues(const Timeline& tl);

/// Adds cues to the "Subtitles" track (created when missing). Cues that
/// would overlap existing ones are shortened to fit. Returns how many were added.
int addSubtitleCues(Timeline& tl, const std::vector<SubtitleCue>& cues);

}  // namespace lectern::timeline
