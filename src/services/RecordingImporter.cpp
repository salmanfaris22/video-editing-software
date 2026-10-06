#include "services/RecordingImporter.h"

#include "core/Log.h"
#include "media/MediaProbe.h"
#include "project/ProjectStore.h"

#include <algorithm>

namespace lectern::services {

namespace {

using project::MediaRole;

MediaRole roleFor(capture::TrackRole r) {
    switch (r) {
        case capture::TrackRole::Screen: return MediaRole::Screen;
        case capture::TrackRole::Camera: return MediaRole::Camera;
        case capture::TrackRole::Phone: return MediaRole::Phone;
        case capture::TrackRole::Microphone: return MediaRole::Microphone;
        case capture::TrackRole::SystemAudio: return MediaRole::SystemAudio;
    }
    return MediaRole::Imported;
}

std::string trackName(const capture::ManifestTrack& t) {
    switch (t.role) {
        case capture::TrackRole::Screen: return "Screen";
        case capture::TrackRole::Camera: return t.sourceName.empty() ? "Camera" : t.sourceName;
        case capture::TrackRole::Phone: return "Phone Camera";
        case capture::TrackRole::Microphone: return "Microphone";
        case capture::TrackRole::SystemAudio: return "System Audio";
    }
    return t.id;
}

// Track order: screen at the bottom, cameras above it, phone on top; audio
// tracks after video (microphone first).
int stackOrder(capture::TrackRole r) {
    switch (r) {
        case capture::TrackRole::Screen: return 0;
        case capture::TrackRole::Camera: return 1;
        case capture::TrackRole::Phone: return 2;
        case capture::TrackRole::Microphone: return 10;
        case capture::TrackRole::SystemAudio: return 11;
    }
    return 5;
}

project::CanvasSettings canvasFor(int width, int height, FrameRate fps) {
    project::CanvasSettings c;
    if (height <= 0) height = 1080;
    if (height <= 720) {
        c.width = 1280;
        c.height = 720;
    } else if (height <= 1080) {
        c.width = 1920;
        c.height = 1080;
    } else if (height <= 1440) {
        c.width = 2560;
        c.height = 1440;
    } else {
        c.width = 3840;
        c.height = 2160;
    }
    (void)width;
    c.frameRate = fps.isValid() ? fps : FrameRate(30, 1);
    return c;
}

}  // namespace

Result<project::Project> buildProjectFromSession(const capture::SessionManifest& manifest,
                                                 const std::filesystem::path& projectDir,
                                                 const ImportOptions& options) {
    project::Project p = project::Project::createEmpty(manifest.title.empty() ? "Recording" : manifest.title);

    std::vector<const capture::ManifestTrack*> usable;
    for (const auto& t : manifest.tracks) {
        if (t.state == capture::TrackState::Completed || t.state == capture::TrackState::Recovered) usable.push_back(&t);
    }
    if (usable.empty()) return fail(ErrorCode::NotFound, "the recording contains no usable media");
    std::stable_sort(usable.begin(), usable.end(),
                     [](const auto* a, const auto* b) { return stackOrder(a->role) < stackOrder(b->role); });

    const timeline::LinkGroupId link = timeline::LinkGroupId::generate();
    const capture::ManifestTrack* screen = nullptr;
    const capture::ManifestTrack* camera = nullptr;

    for (const capture::ManifestTrack* t : usable) {
        project::MediaSource m;
        m.id = project::MediaId::generate();
        m.kind = t->mediaType == capture::MediaType::Video ? project::MediaKind::Video : project::MediaKind::Audio;
        m.role = roleFor(t->role);
        m.name = trackName(*t);
        m.path = t->file;
        m.recording = project::RecordingRef{manifest.sessionId, t->id};
        const std::filesystem::path abs = projectDir / t->file;

        Time start = t->start.value_or(Time::zero());
        Time end = t->end.value_or(manifest.duration);
        if (options.probeMedia) {
            auto info = media::probeMedia(abs);
            if (!info) {
                LEC_WARN("import", "skipping track {}: {}", t->id, info.error().toString());
                continue;
            }
            m.info.container = info->container;
            m.info.start = info->start;
            m.info.duration = info->duration;
            if (const auto* vs = info->video()) {
                m.info.video = project::VideoMetadata{vs->codec,
                                                      vs->video->width,
                                                      vs->video->height,
                                                      vs->video->averageFrameRate,
                                                      vs->video->pixelFormat,
                                                      vs->video->colorSpace,
                                                      vs->video->colorRange,
                                                      vs->video->rotationDegrees,
                                                      vs->video->variableFrameRate,
                                                      vs->video->colorPrimaries,
                                                      vs->video->colorTransfer};
            }
            if (const auto* as = info->audio()) {
                m.info.audio = project::AudioMetadata{as->codec, as->audio->sampleRate, as->audio->channels};
            }
            if (!t->start) start = info->start;
            if (!t->end) end = info->start + info->duration;
        } else {
            m.info.start = start;
            m.info.duration = end - start;
        }
        if (options.computeFingerprints) {
            if (auto fp = project::computeFingerprint(abs)) m.fingerprint = *fp;
        }
        if (end <= start) {
            LEC_WARN("import", "skipping empty track {}", t->id);
            continue;
        }

        timeline::Track track;
        track.id = timeline::TrackId::generate();
        track.kind = t->mediaType == capture::MediaType::Video ? timeline::TrackKind::Video : timeline::TrackKind::Audio;
        track.name = m.name;

        timeline::Clip clip;
        clip.id = timeline::ClipId::generate();
        clip.kind = timeline::ClipKind::Media;
        clip.name = m.name;
        clip.linkGroup = link;
        clip.media = m.id;
        // Recorded files carry session time as media time, so the clip starts
        // at the track's first timestamp and media time == timeline time.
        clip.range = {start, end - start};
        clip.sourceIn = start;
        // Placement comes from the layout preset; the clip transform stays the
        // identity (a user adjustment relative to the preset's slot).
        if (auto st = track.insertClip(std::move(clip)); !st) return fail(std::move(st).error());

        if (t->role == capture::TrackRole::Screen && !screen) screen = t;
        if ((t->role == capture::TrackRole::Camera || t->role == capture::TrackRole::Phone) && !camera) camera = t;
        p.media.push_back(std::move(m));
        p.timeline.tracks.push_back(std::move(track));
    }
    if (p.timeline.tracks.empty()) return fail(ErrorCode::NotFound, "the recording contains no usable media");

    // Canvas from the screen (or the first video) track.
    const capture::ManifestTrack* canvasSource = screen ? screen : camera;
    if (canvasSource) {
        p.canvas = canvasFor(canvasSource->width, canvasSource->height, canvasSource->frameRate);
    }
    p.exportSettings.width = p.canvas.width;
    p.exportSettings.height = p.canvas.height;

    for (const auto& pause : manifest.pauses) {
        p.timeline.markers.push_back({timeline::MarkerId::generate(), pause.sessionTime, "Paused", "#F5A524",
                                      timeline::MarkerKind::Pause});
    }

    const Time total = p.timeline.duration();
    std::string preset = "screen.only";
    if (screen && camera) {
        preset = "pip.bottom-right.rounded";
    } else if (camera) {
        preset = "camera.only";
    }
    p.timeline.layout.push_back({timeline::LayoutRegionId::generate(), {Time::zero(), total}, preset});

    std::string state = manifest.recovered ? "recovered" : std::string(capture::toString(manifest.state));
    project::RecordingEntry rec{manifest.sessionId, "recordings/" + manifest.sessionId + "/session.json",
                                manifest.inputEventsFile, manifest.createdAtUtc, manifest.duration, state};
    p.recordings.push_back(std::move(rec));
    LEC_TRY(p.validate());
    return p;
}

Result<project::Project> importRecording(const std::filesystem::path& projectDir,
                                         const capture::SessionManifest& manifest, const ImportOptions& options) {
    auto p = buildProjectFromSession(manifest, projectDir, options);
    if (!p) return p;
    LEC_TRY(project::ProjectStore::save(projectDir, *p));
    LEC_INFO("import", "project '{}' created with {} track(s), duration {}", p->title, p->timeline.tracks.size(),
             p->timeline.duration().toString());
    return p;
}

Result<std::vector<RecoveredProject>> recoverInterruptedRecordings(const std::filesystem::path& registryDir) {
    capture::RecoveryService service(registryDir);
    auto sessions = service.findInterruptedSessions();
    if (!sessions) return fail(std::move(sessions).error());
    std::vector<RecoveredProject> out;
    for (const auto& entry : *sessions) {
        auto report = service.recover(entry);
        if (!report) {
            LEC_ERROR("recovery", "session {}: {}", entry.sessionId, report.error().toString());
            continue;
        }
        RecoveredProject rp{entry.projectDir, *report, std::nullopt};
        if (report->tracksRecovered > 0 ||
            std::any_of(report->manifest.tracks.begin(), report->manifest.tracks.end(),
                        [](const auto& t) { return t.state == capture::TrackState::Completed; })) {
            if (auto proj = importRecording(entry.projectDir, report->manifest)) rp.project = std::move(*proj);
        }
        out.push_back(std::move(rp));
    }
    return out;
}

}  // namespace lectern::services
