#include "timeline/Subtitles.h"

#include <algorithm>
#include <cstdio>

namespace lectern::timeline {

namespace {

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            lines.push_back(std::move(current));
            current.clear();
        } else if (ch == '\n') {
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current += ch;
        }
    }
    lines.push_back(std::move(current));
    return lines;
}

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t");
    return std::string(s.substr(b, e - b + 1));
}

/// "hh:mm:ss,mmm", "hh:mm:ss.mmm" or "mm:ss.mmm".
std::optional<Time> parseTimestamp(std::string_view s) {
    int parts[4] = {0, 0, 0, 0};
    int count = 0;
    int digits = 0;
    int fractionDigits = 0;
    bool inFraction = false;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') {
            if (inFraction) {
                if (fractionDigits < 3) parts[3] = parts[3] * 10 + (ch - '0');
                ++fractionDigits;
            } else {
                if (count >= 3) return std::nullopt;
                parts[count] = parts[count] * 10 + (ch - '0');
                ++digits;
            }
        } else if (ch == ':' && !inFraction) {
            if (digits == 0) return std::nullopt;
            ++count;
            digits = 0;
        } else if ((ch == ',' || ch == '.') && !inFraction) {
            inFraction = true;
        } else {
            return std::nullopt;
        }
    }
    if (digits == 0 && !inFraction) return std::nullopt;
    ++count;  // number of h/m/s fields seen
    if (count < 2 || count > 3) return std::nullopt;
    while (fractionDigits > 0 && fractionDigits < 3) {
        parts[3] *= 10;
        ++fractionDigits;
    }
    const int hours = count == 3 ? parts[0] : 0;
    const int minutes = count == 3 ? parts[1] : parts[0];
    const int seconds = count == 3 ? parts[2] : parts[1];
    if (minutes > 59 || seconds > 59) return std::nullopt;
    return Time::fromMilliseconds(((static_cast<std::int64_t>(hours) * 60 + minutes) * 60 + seconds) * 1000 + parts[3]);
}

std::string stripTags(const std::string& s) {
    std::string out;
    bool inTag = false;
    for (char ch : s) {
        if (ch == '<') {
            inTag = true;
        } else if (ch == '>' && inTag) {
            inTag = false;
        } else if (!inTag) {
            out += ch;
        }
    }
    // The common entities WebVTT requires escaping.
    auto replaceAll = [&out](std::string_view from, std::string_view to) {
        for (std::size_t pos = 0; (pos = out.find(from, pos)) != std::string::npos; pos += to.size()) {
            out.replace(pos, from.size(), to);
        }
    };
    replaceAll("&lt;", "<");
    replaceAll("&gt;", ">");
    replaceAll("&nbsp;", " ");
    replaceAll("&amp;", "&");
    return out;
}

std::string formatTimestamp(Time t, char fractionSeparator) {
    const std::int64_t totalMs = std::max<std::int64_t>(0, t.toMilliseconds());
    const std::int64_t ms = totalMs % 1000;
    const std::int64_t s = (totalMs / 1000) % 60;
    const std::int64_t m = (totalMs / 60'000) % 60;
    const std::int64_t h = totalMs / 3'600'000;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld%c%03lld", static_cast<long long>(h), static_cast<long long>(m),
                  static_cast<long long>(s), fractionSeparator, static_cast<long long>(ms));
    return buf;
}

}  // namespace

Result<std::vector<SubtitleCue>> parseSubtitles(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    const std::vector<std::string> lines = splitLines(text);
    std::vector<SubtitleCue> cues;
    std::size_t i = 0;
    while (i < lines.size()) {
        // Find the next timing line ("start --> end [settings]").
        const std::size_t arrow = lines[i].find("-->");
        if (arrow == std::string::npos) {
            ++i;
            continue;
        }
        const std::string startText = trim(std::string_view(lines[i]).substr(0, arrow));
        std::string rest = trim(std::string_view(lines[i]).substr(arrow + 3));
        const std::string endText = rest.substr(0, rest.find_first_of(" \t"));
        const auto start = parseTimestamp(startText);
        const auto end = parseTimestamp(endText);
        ++i;
        std::string body;
        while (i < lines.size() && !trim(lines[i]).empty()) {
            if (lines[i].find("-->") != std::string::npos) break;  // a cue without text
            if (!body.empty()) body += '\n';
            body += lines[i];
            ++i;
        }
        if (!start || !end || *end <= *start) continue;  // malformed cue: skip it
        std::string clean = trim(stripTags(body));
        if (clean.empty()) continue;
        cues.push_back({TimeRange::fromStartEnd(*start, *end), std::move(clean)});
    }
    if (cues.empty()) return fail(ErrorCode::ParseError, "no subtitle cues found (expected SRT or WebVTT)");
    std::stable_sort(cues.begin(), cues.end(), [](const SubtitleCue& a, const SubtitleCue& b) {
        return a.range.start < b.range.start;
    });
    return cues;
}

std::string writeSrt(const std::vector<SubtitleCue>& cues) {
    std::string out;
    int n = 0;
    for (const SubtitleCue& c : cues) {
        out += std::to_string(++n) + "\n" + formatTimestamp(c.range.start, ',') + " --> " +
               formatTimestamp(c.range.end(), ',') + "\n" + c.text + "\n\n";
    }
    return out;
}

std::string writeVtt(const std::vector<SubtitleCue>& cues) {
    std::string out = "WEBVTT\n\n";
    for (const SubtitleCue& c : cues) {
        out += formatTimestamp(c.range.start, '.') + " --> " + formatTimestamp(c.range.end(), '.') + "\n" + c.text +
               "\n\n";
    }
    return out;
}

std::vector<SubtitleCue> subtitleCues(const Timeline& tl) {
    std::vector<SubtitleCue> cues;
    for (const Track& t : tl.tracks) {
        if (t.kind != TrackKind::Subtitle || t.hidden) continue;
        for (const Clip& c : t.clips) {
            if (c.enabled && c.kind == ClipKind::Subtitle && c.subtitle) cues.push_back({c.range, c.subtitle->text});
        }
    }
    std::stable_sort(cues.begin(), cues.end(), [](const SubtitleCue& a, const SubtitleCue& b) {
        return a.range.start < b.range.start;
    });
    return cues;
}

int addSubtitleCues(Timeline& tl, const std::vector<SubtitleCue>& cues) {
    auto track = std::find_if(tl.tracks.begin(), tl.tracks.end(), [](const Track& t) {
        return t.kind == TrackKind::Subtitle;
    });
    if (track == tl.tracks.end()) {
        Track t;
        t.id = TrackId::generate();
        t.kind = TrackKind::Subtitle;
        t.name = "Subtitles";
        tl.tracks.push_back(std::move(t));
        track = std::prev(tl.tracks.end());
    }
    int added = 0;
    for (const SubtitleCue& cue : cues) {
        TimeRange range = cue.range;
        // Shorten to fit between existing cues.
        for (const Clip& other : track->clips) {
            if (other.range.start <= range.start && other.range.end() > range.start) {
                range = TimeRange::fromStartEnd(other.range.end(), range.end());  // keep the cue's end
            }
        }
        for (const Clip& other : track->clips) {
            if (other.range.start > range.start && other.range.start < range.end()) {
                range = TimeRange::fromStartEnd(range.start, other.range.start);
            }
        }
        if (range.duration < Time::fromMilliseconds(100)) continue;
        Clip clip;
        clip.id = ClipId::generate();
        clip.kind = ClipKind::Subtitle;
        clip.name = "Subtitle";
        clip.range = range;
        clip.subtitle = SubtitleContent{cue.text, "default"};
        if (track->insertClip(std::move(clip))) ++added;
    }
    return added;
}

}  // namespace lectern::timeline
