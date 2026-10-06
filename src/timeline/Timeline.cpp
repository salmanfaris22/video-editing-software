#include "timeline/Timeline.h"

#include <set>

namespace lectern::timeline {

std::string_view toString(TrackKind kind) noexcept {
    switch (kind) {
        case TrackKind::Video: return "video";
        case TrackKind::Audio: return "audio";
        case TrackKind::Overlay: return "overlay";
        case TrackKind::Subtitle: return "subtitle";
    }
    return "video";
}

std::string_view toString(ClipKind kind) noexcept {
    switch (kind) {
        case ClipKind::Media: return "media";
        case ClipKind::Text: return "text";
        case ClipKind::Subtitle: return "subtitle";
        case ClipKind::Color: return "color";
    }
    return "media";
}

std::string_view toString(Interpolation interp) noexcept {
    switch (interp) {
        case Interpolation::Hold: return "hold";
        case Interpolation::Linear: return "linear";
        case Interpolation::EaseIn: return "easeIn";
        case Interpolation::EaseOut: return "easeOut";
        case Interpolation::EaseInOut: return "easeInOut";
        case Interpolation::Bezier: return "bezier";
    }
    return "linear";
}

std::string_view toString(FitMode mode) noexcept {
    switch (mode) {
        case FitMode::Fit: return "fit";
        case FitMode::Fill: return "fill";
        case FitMode::Stretch: return "stretch";
        case FitMode::None: return "none";
    }
    return "fit";
}

std::string_view toString(MarkerKind kind) noexcept {
    switch (kind) {
        case MarkerKind::User: return "user";
        case MarkerKind::Pause: return "pause";
        case MarkerKind::Chapter: return "chapter";
    }
    return "user";
}

const Clip* Track::clipAt(Time t) const {
    // First clip whose start is > t, then step back one.
    auto it = std::upper_bound(clips.begin(), clips.end(), t,
                               [](Time value, const Clip& c) { return value < c.range.start; });
    if (it == clips.begin()) return nullptr;
    --it;
    return it->range.contains(t) ? &*it : nullptr;
}

Status Track::insertClip(Clip clip) {
    if (clip.range.duration <= Time::zero()) return fail(ErrorCode::InvalidArgument, "clip duration must be > 0");
    auto it = std::lower_bound(clips.begin(), clips.end(), clip.range.start,
                               [](const Clip& c, Time value) { return c.range.start < value; });
    if (it != clips.end() && it->range.intersects(clip.range)) {
        return fail(ErrorCode::InvalidArgument, "clip overlaps the following clip");
    }
    if (it != clips.begin() && std::prev(it)->range.intersects(clip.range)) {
        return fail(ErrorCode::InvalidArgument, "clip overlaps the preceding clip");
    }
    clips.insert(it, std::move(clip));
    return ok();
}

Time Timeline::duration() const {
    Time end;
    for (const Track& t : tracks) end = std::max(end, t.end());
    return end;
}

const Clip* Timeline::findClip(const ClipId& id) const {
    for (const Track& t : tracks) {
        for (const Clip& c : t.clips) {
            if (c.id == id) return &c;
        }
    }
    return nullptr;
}

Track* Timeline::findTrack(const TrackId& id) {
    for (Track& t : tracks) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

const Track* Timeline::findTrack(const TrackId& id) const {
    for (const Track& t : tracks) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::vector<ActiveClip> Timeline::activeClipsAt(Time t) const {
    std::vector<ActiveClip> out;
    for (const Track& track : tracks) {
        if (track.kind == TrackKind::Audio || track.hidden) continue;
        if (const Clip* c = track.clipAt(t); c && c->enabled) out.push_back({&track, c, c->sourceTimeAt(t)});
    }
    return out;
}

Status Timeline::validate(const std::function<bool(const MediaId&)>& mediaExists) const {
    std::set<Uuid> ids;
    auto unique = [&](const Uuid& id, const std::string& what) -> Status {
        if (id.isNil()) return fail(ErrorCode::Corrupt, what + ": missing id");
        if (!ids.insert(id).second) return fail(ErrorCode::Corrupt, what + ": duplicate id " + id.toString());
        return ok();
    };
    for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
        const Track& track = tracks[ti];
        const std::string tpath = "timeline.tracks[" + std::to_string(ti) + "]";
        LEC_TRY(unique(track.id.uuid(), tpath));
        for (std::size_t ci = 0; ci < track.clips.size(); ++ci) {
            const Clip& c = track.clips[ci];
            const std::string cpath = tpath + ".clips[" + std::to_string(ci) + "]";
            LEC_TRY(unique(c.id.uuid(), cpath));
            if (c.range.duration <= Time::zero()) return fail(ErrorCode::Corrupt, cpath + ".duration: must be > 0");
            if (!c.speed.isPositive()) return fail(ErrorCode::Corrupt, cpath + ".speed: must be > 0");
            if (ci > 0 && track.clips[ci - 1].range.end() > c.range.start) {
                return fail(ErrorCode::Corrupt, cpath + ": overlaps previous clip or is out of order");
            }
            if (c.kind == ClipKind::Media) {
                if (!c.media.isValid()) return fail(ErrorCode::Corrupt, cpath + ".mediaId: missing");
                if (mediaExists && !mediaExists(c.media)) {
                    return fail(ErrorCode::Corrupt, cpath + ".mediaId: unknown media " + c.media.toString());
                }
                if (c.sourceIn < Time::zero()) return fail(ErrorCode::Corrupt, cpath + ".sourceIn: negative");
            }
        }
    }
    for (const Marker& m : markers) LEC_TRY(unique(m.id.uuid(), "timeline.markers"));
    for (const LayoutRegion& r : layout) LEC_TRY(unique(r.id.uuid(), "timeline.layout"));
    return ok();
}

}  // namespace lectern::timeline
