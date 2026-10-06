#include "timeline/EditOps.h"

#include <algorithm>
#include <map>

namespace lectern::timeline::edit {

namespace {

template <class T>
void shiftKeys(Animated<T>& a, Time delta) {
    for (auto& k : a.keys) k.time += delta;
}

/// Clip-local keyframe times move by `delta` (negative when the front is trimmed).
void shiftAllKeys(Clip& c, Time delta) {
    if (delta.isZero()) return;
    shiftKeys(c.transform.position, delta);
    shiftKeys(c.transform.scale, delta);
    shiftKeys(c.transform.rotation, delta);
    shiftKeys(c.transform.anchor, delta);
    shiftKeys(c.crop, delta);
    shiftKeys(c.opacity, delta);
    shiftKeys(c.audio.volume, delta);
    ColorAdjustments& col = c.color;
    for (Animated<double>* a : {&col.exposure, &col.brightness, &col.contrast, &col.highlights, &col.shadows,
                                &col.saturation, &col.temperature, &col.tint, &col.gamma, &col.sharpness}) {
        shiftKeys(*a, delta);
    }
    for (auto& e : c.effects) {
        for (auto& [name, param] : e.params) shiftKeys(param, delta);
    }
}

/// Drops the first `amount` of a clip: it starts later in its media.
void trimFront(Clip& c, Time amount) {
    if (c.kind == ClipKind::Media) c.sourceIn += amount.scaled(c.speed);
    c.range.start += amount;
    c.range.duration -= amount;
    shiftAllKeys(c, -amount);
    c.audio.fadeIn = std::min(c.audio.fadeIn, c.range.duration);
}

void trimBack(Clip& c, Time newDuration) {
    c.range.duration = newDuration;
    c.audio.fadeOut = std::min(c.audio.fadeOut, c.range.duration);
}

/// Splits `c` at timeline time t (strictly inside); `c` keeps the left part.
Clip splitClip(Clip& c, Time t) {
    Clip right = c;
    right.id = ClipId::generate();
    trimFront(right, t - c.range.start);
    right.audio.fadeIn = Time::zero();  // a cut point has no fades
    trimBack(c, t - c.range.start);
    c.audio.fadeOut = Time::zero();
    return right;
}

struct ClipRef {
    Track* track;
    std::size_t index;
};

std::optional<ClipRef> locate(Timeline& tl, const ClipId& id) {
    for (Track& t : tl.tracks) {
        for (std::size_t i = 0; i < t.clips.size(); ++i) {
            if (t.clips[i].id == id) return ClipRef{&t, i};
        }
    }
    return std::nullopt;
}

std::size_t groupSize(const Timeline& tl, const LinkGroupId& group) {
    std::size_t n = 0;
    for (const Track& t : tl.tracks) {
        for (const Clip& c : t.clips) n += (c.linkGroup && *c.linkGroup == group) ? 1 : 0;
    }
    return n;
}

/// [earliest start, latest end] over the clips of a link group.
TimeRange groupSpan(const Timeline& tl, const LinkGroupId& group) {
    Time start = Time::max();
    Time end = Time::min();
    for (const Track& t : tl.tracks) {
        for (const Clip& c : t.clips) {
            if (c.linkGroup && *c.linkGroup == group) {
                start = std::min(start, c.range.start);
                end = std::max(end, c.range.end());
            }
        }
    }
    return TimeRange::fromStartEnd(start, end);
}

void mergeLayout(Timeline& tl) {
    std::sort(tl.layout.begin(), tl.layout.end(),
              [](const LayoutRegion& a, const LayoutRegion& b) { return a.range.start < b.range.start; });
    std::vector<LayoutRegion> merged;
    for (LayoutRegion& r : tl.layout) {
        if (r.range.isEmpty()) continue;
        if (!merged.empty() && merged.back().preset == r.preset && merged.back().range.end() >= r.range.start) {
            merged.back().range = TimeRange::fromStartEnd(merged.back().range.start,
                                                          std::max(merged.back().range.end(), r.range.end()));
        } else {
            merged.push_back(std::move(r));
        }
    }
    tl.layout = std::move(merged);
}

/// Inserts `amount` of time at `at` on unlocked tracks: clips starting at or
/// after `at` (other than members of `growing`) move right; markers follow.
/// The new time belongs to the layout region that starts at `at` when
/// `toFollowing` (a segment growing at its head), else to the region that
/// ends there (a segment growing at its tail).
void insertTime(Timeline& tl, Time at, Time amount, const LinkGroupId& growing, bool toFollowing) {
    for (Track& track : tl.tracks) {
        if (track.locked) continue;
        for (Clip& c : track.clips) {
            const bool member = c.linkGroup && *c.linkGroup == growing;
            if (!member && c.range.start >= at) c.range.start += amount;
        }
    }
    for (Marker& m : tl.markers) {
        if (m.time >= at) m.time += amount;
    }
    for (LayoutRegion& r : tl.layout) {
        const Time s = r.range.start;
        const Time e = r.range.end();
        if (s > at || (s == at && !toFollowing)) {
            r.range.start += amount;
        } else if (e > at || (e == at && !toFollowing) || (s == at && toFollowing)) {
            r.range.duration += amount;
        }
    }
}

}  // namespace

std::vector<const Clip*> linkedClips(const Timeline& tl, const ClipId& clip) {
    const Clip* self = tl.findClip(clip);
    if (!self) return {};
    if (!self->linkGroup) return {self};
    std::vector<const Clip*> out;
    for (const Track& t : tl.tracks) {
        for (const Clip& c : t.clips) {
            if (c.linkGroup && *c.linkGroup == *self->linkGroup) out.push_back(&c);
        }
    }
    return out;
}

bool isLinkedSegment(const Timeline& tl, const ClipId& clip) {
    const Clip* c = tl.findClip(clip);
    return c && c->linkGroup && groupSize(tl, *c->linkGroup) > 1;
}

Result<int> splitAt(Timeline& tl, Time t, const std::optional<ClipId>& clip) {
    std::vector<ClipRef> targets;
    auto crosses = [t](const Clip& c) { return c.range.start < t && t < c.range.end(); };
    if (clip) {
        const Clip* self = tl.findClip(*clip);
        if (!self) return fail(ErrorCode::NotFound, "clip not found");
        for (Track& track : tl.tracks) {
            if (track.locked) continue;
            for (std::size_t i = 0; i < track.clips.size(); ++i) {
                const Clip& c = track.clips[i];
                const bool sameGroup = self->linkGroup ? (c.linkGroup && *c.linkGroup == *self->linkGroup) : c.id == *clip;
                if (sameGroup && crosses(c)) targets.push_back({&track, i});
            }
        }
    } else {
        for (int pass = 0; pass < 2 && targets.empty(); ++pass) {
            for (Track& track : tl.tracks) {
                if (track.locked) continue;
                if (pass == 1 && track.kind != TrackKind::Video && track.kind != TrackKind::Audio) continue;
                for (std::size_t i = 0; i < track.clips.size(); ++i) {
                    const Clip& c = track.clips[i];
                    if (crosses(c) && (pass == 1 || c.linkGroup)) targets.push_back({&track, i});
                }
            }
        }
    }
    // Every target must leave two usable halves, or nothing is split.
    for (const ClipRef& r : targets) {
        const Clip& c = r.track->clips[r.index];
        if (t - c.range.start < kMinClipDuration || c.range.end() - t < kMinClipDuration) {
            return fail(ErrorCode::InvalidArgument, "too close to a clip edge to split");
        }
    }
    std::map<LinkGroupId, LinkGroupId> fresh;
    // Insert from the back so earlier indices stay valid.
    std::sort(targets.begin(), targets.end(), [](const ClipRef& a, const ClipRef& b) {
        return a.track != b.track ? a.track < b.track : a.index > b.index;
    });
    for (const ClipRef& r : targets) {
        Clip& left = r.track->clips[r.index];
        Clip right = splitClip(left, t);
        if (left.linkGroup) {
            auto [it, inserted] = fresh.try_emplace(*left.linkGroup, LinkGroupId{});
            if (inserted) it->second = LinkGroupId::generate();
            right.linkGroup = it->second;
        }
        r.track->clips.insert(r.track->clips.begin() + static_cast<std::ptrdiff_t>(r.index) + 1, std::move(right));
    }
    return static_cast<int>(targets.size());
}

Status removeRange(Timeline& tl, TimeRange range) {
    if (range.duration <= Time::zero()) return fail(ErrorCode::InvalidArgument, "empty range");
    if (range.start < Time::zero()) return fail(ErrorCode::InvalidArgument, "range starts before zero");
    const Time a = range.start;
    const Time b = range.end();
    const Time d = range.duration;
    std::map<LinkGroupId, LinkGroupId> fresh;  // right parts of cut groups stay linked together

    for (Track& track : tl.tracks) {
        if (track.locked) continue;
        std::vector<Clip> out;
        out.reserve(track.clips.size() + 1);
        for (Clip& c : track.clips) {
            const Time s = c.range.start;
            const Time e = c.range.end();
            if (e <= a) {  // before
                out.push_back(std::move(c));
            } else if (s >= b) {  // after
                c.range.start -= d;
                out.push_back(std::move(c));
            } else if (s >= a && e <= b) {  // inside: removed
            } else if (s < a && e > b) {    // spans the range: keep both sides
                Clip right = splitClip(c, b);
                right.range.start = a;
                if (right.linkGroup) {
                    auto [it, inserted] = fresh.try_emplace(*right.linkGroup, LinkGroupId{});
                    if (inserted) it->second = LinkGroupId::generate();
                    right.linkGroup = it->second;
                }
                trimBack(c, a - s);
                c.audio.fadeOut = Time::zero();
                out.push_back(std::move(c));
                out.push_back(std::move(right));
            } else if (s < a) {  // overlaps the start
                trimBack(c, a - s);
                out.push_back(std::move(c));
            } else {  // overlaps the end
                trimFront(c, b - s);
                c.range.start = a;
                out.push_back(std::move(c));
            }
        }
        // Drop slivers a cut may leave behind.
        std::erase_if(out, [](const Clip& c) { return c.range.duration < kMinClipDuration; });
        track.clips = std::move(out);
    }

    std::erase_if(tl.markers, [&](const Marker& m) { return m.time >= a && m.time < b; });
    for (Marker& m : tl.markers) {
        if (m.time >= b) m.time -= d;
    }
    std::vector<LayoutRegion> regions;
    for (LayoutRegion& r : tl.layout) {
        const Time s = r.range.start;
        const Time e = r.range.end();
        if (e <= a) {
            regions.push_back(r);
        } else if (s >= b) {
            r.range.start -= d;
            regions.push_back(r);
        } else if (s >= a && e <= b) {
        } else if (s < a && e > b) {
            r.range.duration -= d;
            regions.push_back(r);
        } else if (s < a) {
            r.range.duration = a - s;
            regions.push_back(r);
        } else {
            r.range = TimeRange::fromStartEnd(a, e - d);
            regions.push_back(r);
        }
    }
    tl.layout = std::move(regions);
    mergeLayout(tl);
    return ok();
}

Result<Time> removeRanges(Timeline& tl, std::vector<TimeRange> ranges) {
    std::erase_if(ranges, [](const TimeRange& r) { return r.duration <= Time::zero(); });
    std::sort(ranges.begin(), ranges.end(), [](const TimeRange& x, const TimeRange& y) { return x.start < y.start; });
    std::vector<TimeRange> merged;
    for (const TimeRange& r : ranges) {
        if (!merged.empty() && r.start <= merged.back().end()) {
            merged.back() = TimeRange::fromStartEnd(merged.back().start, std::max(merged.back().end(), r.end()));
        } else {
            merged.push_back(r);
        }
    }
    Timeline work = tl;
    Time removed;
    for (auto it = merged.rbegin(); it != merged.rend(); ++it) {
        LEC_TRY(removeRange(work, *it));
        removed += it->duration;
    }
    tl = std::move(work);
    return removed;
}

Status deleteClip(Timeline& tl, const ClipId& id) {
    const auto ref = locate(tl, id);
    if (!ref) return fail(ErrorCode::NotFound, "clip not found");
    const Clip& c = ref->track->clips[ref->index];
    if (ref->track->locked) return fail(ErrorCode::InvalidState, "the track is locked");
    if (c.linkGroup && groupSize(tl, *c.linkGroup) > 1) {
        const TimeRange span = groupSpan(tl, *c.linkGroup);
        return removeRange(tl, TimeRange::fromStartEnd(std::max(Time::zero(), span.start), span.end()));
    }
    ref->track->clips.erase(ref->track->clips.begin() + static_cast<std::ptrdiff_t>(ref->index));
    return ok();
}

Status deleteClipLocal(Timeline& tl, const ClipId& id) {
    const auto ref = locate(tl, id);
    if (!ref) return fail(ErrorCode::NotFound, "clip not found");
    if (ref->track->locked) return fail(ErrorCode::InvalidState, "the track is locked");
    ref->track->clips.erase(ref->track->clips.begin() + static_cast<std::ptrdiff_t>(ref->index));
    return ok();
}

namespace {
/// Where a clip of `duration` wanting to start at `wanted` fits on `track`:
/// the gap the wanted start falls into decides it, snapping to its edges.
std::optional<Time> placeInGap(const Track& track, Time wanted, Time duration) {
    const Time target = std::max(Time::zero(), wanted);
    Time lo = Time::zero();
    Time hi = Time::max();
    for (const Clip& other : track.clips) {
        if (other.range.start <= target) {
            lo = std::max(lo, other.range.end());
        } else {
            hi = std::min(hi, other.range.start);
        }
    }
    if (hi != Time::max() && hi - lo < duration) return std::nullopt;
    return hi == Time::max() ? std::max(target, lo) : std::clamp(target, lo, hi - duration);
}
}  // namespace

namespace {
/// Slides a recording segment (every clip of its link group, on every track)
/// by the same amount, as far as the neighbours and time zero allow.
Status moveLinkedGroup(Timeline& tl, const ClipId& id, Time newStart) {
    const Clip* self = tl.findClip(id);
    const LinkGroupId group = *self->linkGroup;
    const auto inGroup = [&](const Clip& c) { return c.linkGroup && *c.linkGroup == group; };
    Time lo = -Time::max();
    Time hi = Time::max();
    for (const Track& t : tl.tracks) {
        for (const Clip& m : t.clips) {
            if (!inGroup(m)) continue;
            if (t.locked) return fail(ErrorCode::InvalidState, "a track of this recording is locked");
            lo = std::max(lo, Time::zero() - m.range.start);  // never before 0
            for (const Clip& other : t.clips) {
                if (inGroup(other)) continue;
                if (other.range.start < m.range.start) {
                    lo = std::max(lo, other.range.end() - m.range.start);
                } else {
                    hi = std::min(hi, other.range.start - m.range.end());
                }
            }
        }
    }
    if (hi < lo) return fail(ErrorCode::InvalidArgument, "no room to move the recording");
    const Time delta = std::clamp(std::max(Time::zero(), newStart) - self->range.start, lo, hi);
    if (delta == Time::zero()) return ok();
    for (Track& t : tl.tracks) {
        for (Clip& m : t.clips) {
            if (inGroup(m)) m.range.start = m.range.start + delta;
        }
        std::stable_sort(t.clips.begin(), t.clips.end(), [](const Clip& a, const Clip& b) { return a.range.start < b.range.start; });
    }
    return ok();
}
}  // namespace

Status moveClip(Timeline& tl, const ClipId& id, Time newStart) {
    const auto ref = locate(tl, id);
    if (!ref) return fail(ErrorCode::NotFound, "clip not found");
    if (ref->track->locked) return fail(ErrorCode::InvalidState, "the track is locked");
    if (isLinkedSegment(tl, id)) return moveLinkedGroup(tl, id, newStart);
    Track& track = *ref->track;
    const Time duration = track.clips[ref->index].range.duration;
    Clip moving = std::move(track.clips[ref->index]);
    track.clips.erase(track.clips.begin() + static_cast<std::ptrdiff_t>(ref->index));
    const auto start = placeInGap(track, newStart, duration);
    if (!start) {
        track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(ref->index), std::move(moving));
        return fail(ErrorCode::InvalidArgument, "no room for the clip there");
    }
    moving.range.start = *start;
    LEC_TRY(track.insertClip(std::move(moving)));
    return ok();
}

Status moveClipToTrack(Timeline& tl, const ClipId& id, const TrackId& trackId, Time newStart) {
    const auto ref = locate(tl, id);
    if (!ref) return fail(ErrorCode::NotFound, "clip not found");
    if (ref->track->id == trackId) return moveClip(tl, id, newStart);
    Track* target = tl.findTrack(trackId);
    if (!target) return fail(ErrorCode::NotFound, "track not found");
    if (ref->track->locked || target->locked) return fail(ErrorCode::InvalidState, "the track is locked");
    if (target->kind != ref->track->kind) return fail(ErrorCode::InvalidArgument, "the clip does not belong on that kind of track");
    if (isLinkedSegment(tl, id)) return fail(ErrorCode::Unsupported, "recording segments move with the timeline");
    const Time duration = ref->track->clips[ref->index].range.duration;
    const auto start = placeInGap(*target, newStart, duration);
    if (!start) return fail(ErrorCode::InvalidArgument, "no room for the clip there");
    Clip moving = std::move(ref->track->clips[ref->index]);
    ref->track->clips.erase(ref->track->clips.begin() + static_cast<std::ptrdiff_t>(ref->index));
    moving.range.start = *start;
    LEC_TRY(target->insertClip(std::move(moving)));
    return ok();
}

Status trimStart(Timeline& tl, const ClipId& id, Time edge, const MediaBounds& bounds) {
    const auto ref = locate(tl, id);
    if (!ref) return fail(ErrorCode::NotFound, "clip not found");
    if (ref->track->locked) return fail(ErrorCode::InvalidState, "the track is locked");
    const Clip clip = ref->track->clips[ref->index];

    if (clip.linkGroup && groupSize(tl, *clip.linkGroup) > 1) {
        const TimeRange span = groupSpan(tl, *clip.linkGroup);
        if (edge > span.start) {  // shorten: ripple-remove the head of the segment
            edge = std::min(edge, clip.range.end() - kMinClipDuration);
            if (edge <= span.start) return ok();
            return removeRange(tl, TimeRange::fromStartEnd(span.start, edge));
        }
        // Lengthen: reveal earlier media in every member while the segment
        // keeps its place (so `edge` may be negative); later content moves right.
        Time grow = span.start - edge;
        for (const Clip* member : linkedClips(tl, id)) {
            if (member->kind != ClipKind::Media || !bounds) continue;
            if (const auto avail = bounds(member->media)) {
                grow = std::min(grow, (member->sourceIn - avail->start).scaled(member->speed.inverse()));
            }
        }
        if (grow <= Time::zero()) return ok();
        const LinkGroupId group = *clip.linkGroup;
        insertTime(tl, span.start, grow, group, true);
        for (Track& track : tl.tracks) {
            for (Clip& c : track.clips) {
                if (!c.linkGroup || *c.linkGroup != group) continue;
                // Members keep their offsets relative to the segment start.
                if (c.kind == ClipKind::Media) c.sourceIn -= grow.scaled(c.speed);
                c.range.duration += grow;
                shiftAllKeys(c, grow);
            }
        }
        return ok();
    }

    // Free clip: bounded by the previous clip, the media and the minimum length.
    Clip& c = ref->track->clips[ref->index];
    Time lo = ref->index > 0 ? ref->track->clips[ref->index - 1].range.end() : Time::zero();
    if (c.kind == ClipKind::Media && bounds) {
        if (const auto avail = bounds(c.media)) {
            lo = std::max(lo, c.range.start - (c.sourceIn - avail->start).scaled(c.speed.inverse()));
        }
    }
    edge = std::clamp(edge, lo, c.range.end() - kMinClipDuration);
    const Time delta = edge - c.range.start;
    if (delta.isZero()) return ok();
    trimFront(c, delta);  // negative delta reveals earlier media
    return ok();
}

Status trimEnd(Timeline& tl, const ClipId& id, Time edge, const MediaBounds& bounds) {
    const auto ref = locate(tl, id);
    if (!ref) return fail(ErrorCode::NotFound, "clip not found");
    if (ref->track->locked) return fail(ErrorCode::InvalidState, "the track is locked");
    const Clip clip = ref->track->clips[ref->index];

    if (clip.linkGroup && groupSize(tl, *clip.linkGroup) > 1) {
        const TimeRange span = groupSpan(tl, *clip.linkGroup);
        if (edge < span.end()) {  // shorten: ripple-remove the tail
            edge = std::max(edge, clip.range.start + kMinClipDuration);
            if (edge >= span.end()) return ok();
            return removeRange(tl, TimeRange::fromStartEnd(edge, span.end()));
        }
        Time grow = edge - span.end();
        for (const Clip* member : linkedClips(tl, id)) {
            if (member->kind != ClipKind::Media || !bounds) continue;
            if (const auto avail = bounds(member->media)) {
                const Time sourceOut = member->sourceIn + member->sourceDuration();
                grow = std::min(grow, (avail->end() - sourceOut).scaled(member->speed.inverse()));
            }
        }
        if (grow <= Time::zero()) return ok();
        const LinkGroupId group = *clip.linkGroup;
        insertTime(tl, span.end(), grow, group, false);
        // Every member grows by the same amount so the segment stays aligned
        // (members may end a frame apart, e.g. camera vs microphone).
        for (Track& track : tl.tracks) {
            for (Clip& c : track.clips) {
                if (c.linkGroup && *c.linkGroup == group) c.range.duration += grow;
            }
        }
        return ok();
    }

    Clip& c = ref->track->clips[ref->index];
    Time hi = ref->index + 1 < ref->track->clips.size() ? ref->track->clips[ref->index + 1].range.start : Time::max();
    if (c.kind == ClipKind::Media && bounds) {
        if (const auto avail = bounds(c.media)) {
            const Time sourceOut = c.sourceIn + c.sourceDuration();
            hi = std::min(hi, c.range.end() + (avail->end() - sourceOut).scaled(c.speed.inverse()));
        }
    }
    edge = std::clamp(edge, c.range.start + kMinClipDuration, hi);
    trimBack(c, edge - c.range.start);
    return ok();
}

std::string layoutAt(const Timeline& tl, Time t) {
    const LayoutRegion* best = nullptr;
    for (const LayoutRegion& r : tl.layout) {
        if (r.range.contains(t)) return r.preset;
        if (r.range.start <= t && (!best || r.range.start > best->range.start)) best = &r;
    }
    if (best) return best->preset;
    return tl.layout.empty() ? std::string("screen.only") : tl.layout.front().preset;
}

void setLayoutAll(Timeline& tl, const std::string& preset) {
    const Time duration = std::max(tl.duration(), Time::fromSeconds(1));
    tl.layout.clear();
    tl.layout.push_back({LayoutRegionId::generate(), {Time::zero(), duration}, preset});
}

Status setLayoutFrom(Timeline& tl, Time t, const std::string& preset) {
    if (preset.empty()) return fail(ErrorCode::InvalidArgument, "empty layout preset");
    const Time duration = std::max(tl.duration(), t + Time::fromSeconds(1));
    t = std::max(Time::zero(), t);
    if (tl.layout.empty()) tl.layout.push_back({LayoutRegionId::generate(), {Time::zero(), duration}, layoutAt(tl, t)});
    // Extend the last region to the end of the timeline so `t` is covered.
    auto last = std::max_element(tl.layout.begin(), tl.layout.end(), [](const LayoutRegion& x, const LayoutRegion& y) {
        return x.range.start < y.range.start;
    });
    if (last->range.end() < duration) last->range.duration = duration - last->range.start;

    std::vector<LayoutRegion> out;
    bool applied = false;
    for (const LayoutRegion& r : tl.layout) {
        if (!r.range.contains(t)) {
            out.push_back(r);
            continue;
        }
        if (r.range.start < t) {
            out.push_back({r.id, TimeRange::fromStartEnd(r.range.start, t), r.preset});
        }
        out.push_back({LayoutRegionId::generate(), TimeRange::fromStartEnd(t, r.range.end()), preset});
        applied = true;
    }
    if (!applied) {  // `t` falls into a gap: fill it up to the next region
        Time next = duration;
        for (const LayoutRegion& r : tl.layout) {
            if (r.range.start > t) next = std::min(next, r.range.start);
        }
        out.push_back({LayoutRegionId::generate(), TimeRange::fromStartEnd(t, next), preset});
    }
    tl.layout = std::move(out);
    mergeLayout(tl);
    return ok();
}

}  // namespace lectern::timeline::edit
