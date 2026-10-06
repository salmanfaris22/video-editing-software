#include "capture/RecordingSession.h"

#include "core/Json.h"
#include "core/Log.h"
#include "core/Thread.h"
#include "core/Uuid.h"

#include <algorithm>
#include <set>

namespace lectern::capture {

namespace {
std::string shortId(const std::string& sessionId) {
    // UUIDv7 time prefix is shared by sessions started in the same minute;
    // the random tail is the distinguishing part.
    return sessionId.size() >= 8 ? sessionId.substr(sessionId.size() - 8) : sessionId;
}
}  // namespace

Result<std::unique_ptr<RecordingSession>> RecordingSession::create(SessionConfig config,
                                                                   IRecordingObserver* observer) {
    if (config.projectDir.empty()) return fail(ErrorCode::InvalidArgument, "session needs a project directory");
    if (config.tracks.empty()) return fail(ErrorCode::InvalidArgument, "session needs at least one track");
    std::set<std::string> ids;
    for (const TrackPlan& t : config.tracks) {
        if (t.trackId.empty() || !ids.insert(t.trackId).second) {
            return fail(ErrorCode::InvalidArgument, "track ids must be unique and non-empty: '" + t.trackId + "'");
        }
        const bool wantsVideo = mediaTypeOf(t.role) == MediaType::Video;
        if (wantsVideo && !t.videoSource) return fail(ErrorCode::InvalidArgument, t.trackId + ": missing video source");
        if (!wantsVideo && !t.audioSource) return fail(ErrorCode::InvalidArgument, t.trackId + ": missing audio source");
    }
    if (config.sessionId.empty()) config.sessionId = Uuid::generateV7().toString();
    if (config.registryDir.empty()) config.registryDir = ActiveSessionRegistry::defaultDirectory();
    return std::unique_ptr<RecordingSession>(new RecordingSession(std::move(config), observer));
}

RecordingSession::RecordingSession(SessionConfig config, IRecordingObserver* observer)
    : config_(std::move(config)),
      observer_(observer),
      hostClock_(config_.clock ? *config_.clock : static_cast<const IClock&>(HostClock::shared())),
      clock_(hostClock_),
      registry_(config_.registryDir),
      disk_(config_.disk, config_.diskQuery ? config_.diskQuery : DiskSpaceMonitor::Query(&fs::queryDiskSpace)) {}

RecordingSession::~RecordingSession() {
    const SessionState s = state();
    if (s == SessionState::Recording || s == SessionState::Paused) (void)stop(StopReason::User);
    if (finalizer_.joinable()) finalizer_.join();
    if (monitor_.joinable()) {
        monitor_.request_stop();
        cv_.notify_all();
        monitor_.join();
    }
    // Detach any writer still attached (failed start paths).
    for (Track& t : tracks_) {
        if (t.plan.videoSource) t.plan.videoSource->setConsumer(nullptr);
        if (t.plan.audioSource) t.plan.audioSource->setConsumer(nullptr);
    }
}

std::filesystem::path RecordingSession::sessionDir() const {
    return config_.projectDir / "recordings" / config_.sessionId;
}

std::filesystem::path RecordingSession::manifestPath() const { return sessionDir() / "session.json"; }

SessionState RecordingSession::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void RecordingSession::setState(SessionState s) {
    {
        std::lock_guard lock(mutex_);
        if (state_ == s) return;
        state_ = s;
    }
    cv_.notify_all();
    LEC_INFO("capture", "session {} → {}", config_.sessionId, toString(s));
    if (observer_) observer_->onStateChanged(s);
}

Status RecordingSession::start() {
    {
        std::lock_guard lock(mutex_);
        if (state_ != SessionState::Idle) return fail(ErrorCode::InvalidState, "session already started");
    }
    const std::filesystem::path sdir = sessionDir();
    LEC_TRY(fs::ensureDirectory(sdir));
    auto lock = fs::FileLock::tryAcquire(sdir / "session.lock");
    if (!lock) return fail(std::move(lock).error());
    lock_ = std::move(*lock);

    // Create writers (files are created now: permission/disk errors surface
    // before the user believes recording has started).
    const std::string sid = shortId(config_.sessionId);
    for (TrackPlan& plan : config_.tracks) {
        Track t;
        t.plan = plan;
        const std::string folder(mediaFolder(plan.role));
        LEC_TRY(fs::ensureDirectory(config_.projectDir / "media" / folder));
        const std::string fileName = plan.trackId + "-" + sid + ".mkv";
        t.relativeFile = "media/" + folder + "/" + fileName;
        const std::filesystem::path out = config_.projectDir / "media" / folder / fileName;
        if (plan.videoSource) {
            VideoTrackSettings vs = plan.video;
            vs.trackId = plan.trackId;
            vs.role = plan.role;
            vs.outputPath = out;
            t.video = std::make_unique<VideoTrackWriter>(std::move(vs), clock_);
            if (auto st = t.video->open(); !st) {
                releaseResources();
                return st;
            }
            if (auto seed = plan.videoSource->latestFrame()) t.video->seed(std::move(*seed));
        } else {
            AudioTrackSettings as = plan.audio;
            as.trackId = plan.trackId;
            as.role = plan.role;
            as.outputPath = out;
            t.audio = std::make_unique<AudioTrackWriter>(std::move(as), clock_, plan.audioSource->info().channels);
            if (auto st = t.audio->open(); !st) {
                releaseResources();
                return st;
            }
        }
        tracks_.push_back(std::move(t));
    }

    // Attach before T0: writers drop anything captured before the clock
    // starts, so data straddling T0 is cut exactly at T0 (no lost first chunk).
    for (Track& t : tracks_) {
        if (t.video) t.plan.videoSource->setConsumer(t.video.get());
        if (t.audio) t.plan.audioSource->setConsumer(t.audio.get());
    }

    {
        std::lock_guard l(mutex_);
        createdAt_ = json::utcNowIso8601();
        tracksReady_ = true;
    }
    const std::int64_t t0 = clock_.start();
    (void)registry_.add({config_.sessionId, config_.projectDir, sdir, createdAt_});
    setState(SessionState::Recording);
    writeCheckpoint();
    LEC_INFO("capture", "session {} recording {} track(s) into {} (T0={} ns)", config_.sessionId, tracks_.size(),
             config_.projectDir.string(), t0);
    monitor_ = std::jthread([this](std::stop_token st) { monitorLoop(st); });
    return ok();
}

void RecordingSession::notifyState(SessionState s) {
    cv_.notify_all();
    LEC_INFO("capture", "session {} → {}", config_.sessionId, toString(s));
    if (observer_) observer_->onStateChanged(s);
}

Status RecordingSession::pause() {
    {
        // Check-and-transition under one lock so a concurrent stop() can never
        // be overwritten by a late pause.
        std::lock_guard lock(mutex_);
        if (state_ != SessionState::Recording) return fail(ErrorCode::InvalidState, "not recording");
        state_ = SessionState::Paused;
        checkpointRequested_ = true;
        clock_.pause();
    }
    notifyState(SessionState::Paused);
    return ok();
}

Status RecordingSession::resume() {
    {
        std::lock_guard lock(mutex_);
        if (state_ != SessionState::Paused) return fail(ErrorCode::InvalidState, "not paused");
        state_ = SessionState::Recording;
        checkpointRequested_ = true;
        clock_.resume();
    }
    for (Track& t : tracks_) {
        if (t.video) t.video->onResumed();
    }
    notifyState(SessionState::Recording);
    return ok();
}

Status RecordingSession::stop(StopReason reason) {
    {
        std::lock_guard lock(mutex_);
        if (state_ != SessionState::Recording && state_ != SessionState::Paused) {
            return state_ == SessionState::Stopping ? ok() : fail(ErrorCode::InvalidState, "not recording");
        }
        stopReason_ = reason;
        state_ = SessionState::Stopping;  // exactly one caller gets here
        clock_.stop();
    }
    LEC_INFO("capture", "session {} stopping ({}), duration {}", config_.sessionId, toString(reason),
             clock_.stopSessionTime().value_or(Time::zero()).toString());
    for (Track& t : tracks_) {
        if (t.video) t.video->requestStop();
    }
    notifyState(SessionState::Stopping);
    finalizer_ = std::jthread([this] { finalize(); });
    return ok();
}

void RecordingSession::finalize() {
    setCurrentThreadName("lectern.session.finalize");
    const std::int64_t stopHost = clock_.stopHostNs().value_or(hostClock_.nowNs());

    // Audio: wait until each writer has processed audio up to S (or the
    // device-side drain timeout passes), then detach and finish.
    const auto realDeadline = std::chrono::steady_clock::now() + config_.maxAudioDrainWait;
    for (Track& t : tracks_) {
        if (!t.audio) continue;
        const std::int64_t drainNs = t.audio->settings().drainTimeout.toNanoseconds();
        while (!t.audio->reachedStop() && hostClock_.nowNs() < stopHost + drainNs &&
               std::chrono::steady_clock::now() < realDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        t.plan.audioSource->setConsumer(nullptr);
        if (auto st = t.audio->finish(); !st) LEC_WARN("capture", "track {}: {}", t.plan.trackId, st.error().toString());
    }
    // Video: writers drain and finalize on their own threads.
    for (Track& t : tracks_) {
        if (!t.video) continue;
        if (!t.video->waitFinished(std::chrono::seconds(120))) {
            LEC_ERROR("capture", "track {}: writer did not finish in time", t.plan.trackId);
        }
        t.plan.videoSource->setConsumer(nullptr);
    }

    SessionManifest manifest = buildManifest(true);
    bool anyMedia = false;
    for (const ManifestTrack& mt : manifest.tracks) anyMedia |= mt.state == TrackState::Completed;
    manifest.state = anyMedia ? SessionState::Completed : SessionState::Failed;
    {
        std::lock_guard lock(manifestWriteMutex_);
        if (auto st = writeManifest(manifestPath(), manifest); !st) {
            LEC_ERROR("capture", "writing final manifest failed: {}", st.error().toString());
        }
    }
    (void)registry_.remove(config_.sessionId);
    lock_.release();

    SessionResult result;
    result.state = manifest.state;
    result.reason = manifest.stopReason;
    result.manifest = manifest;
    result.manifestPath = manifestPath();
    result.projectDir = config_.projectDir;
    {
        std::lock_guard lock(mutex_);
        result_ = result;
    }
    if (monitor_.joinable()) {
        monitor_.request_stop();
        cv_.notify_all();
    }
    setState(manifest.state);
    if (observer_) observer_->onFinished(result);
}

Result<SessionResult> RecordingSession::waitForCompletion(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return result_.has_value() || state_ == SessionState::Cancelled; })) {
        return fail(ErrorCode::Timeout, "recording did not finish in time");
    }
    if (state_ == SessionState::Cancelled) return fail(ErrorCode::Cancelled, "recording was cancelled");
    return *result_;
}

void RecordingSession::cancel() {
    SessionState s = state();
    if (s == SessionState::Idle || s == SessionState::Cancelled) return;
    if (s == SessionState::Recording || s == SessionState::Paused) clock_.stop();
    if (finalizer_.joinable()) finalizer_.join();  // a stop in progress completes first
    if (monitor_.joinable()) {
        monitor_.request_stop();
        cv_.notify_all();
        monitor_.join();
    }
    for (Track& t : tracks_) {
        if (t.video) {
            t.plan.videoSource->setConsumer(nullptr);
            t.video->abort();
        }
        if (t.audio) {
            t.plan.audioSource->setConsumer(nullptr);
            t.audio->abort();
        }
        std::error_code ec;
        std::filesystem::remove(config_.projectDir / t.relativeFile, ec);
    }
    lock_.release();
    std::error_code ec;
    std::filesystem::remove_all(sessionDir(), ec);
    (void)registry_.remove(config_.sessionId);
    {
        std::lock_guard lock(mutex_);
        stopReason_ = StopReason::Cancelled;
    }
    setState(SessionState::Cancelled);
}

void RecordingSession::releaseResources() {
    for (Track& t : tracks_) {
        if (t.video) t.video->abort();
        if (t.audio) t.audio->abort();
    }
    tracks_.clear();
    lock_.release();
}

std::uint64_t RecordingSession::totalBytes() const {
    std::uint64_t total = 0;
    for (const Track& t : tracks_) {
        if (t.video) total += t.video->bytesWritten();
        if (t.audio) total += t.audio->bytesWritten();
    }
    return total;
}

void RecordingSession::writeCheckpoint() {
    SessionManifest m = buildManifest(false);
    std::lock_guard lock(manifestWriteMutex_);
    if (auto st = writeManifest(manifestPath(), m); !st) {
        LEC_WARN("capture", "checkpoint failed: {}", st.error().toString());
    }
}

void RecordingSession::monitorLoop(std::stop_token stopToken) {
    setCurrentThreadName("lectern.session.monitor");
    using SteadyClock = std::chrono::steady_clock;
    auto lastDisk = SteadyClock::now() - config_.diskInterval;
    auto lastCheckpoint = SteadyClock::now();
    DiskLevel reportedLevel = DiskLevel::Ok;

    while (!stopToken.stop_requested()) {
        {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, config_.monitorInterval,
                         [&] { return stopToken.stop_requested() || checkpointRequested_; });
        }
        if (stopToken.stop_requested()) break;
        const SessionState s = state();
        const bool active = s == SessionState::Recording || s == SessionState::Paused;
        const auto now = SteadyClock::now();

        // Disk space.
        if (now - lastDisk >= config_.diskInterval) {
            lastDisk = now;
            const DiskSpaceMonitor::Status ds = disk_.update(config_.projectDir, totalBytes(), hostClock_.nowNs());
            {
                std::lock_guard lock(mutex_);
                diskStatus_ = ds;
            }
            if (ds.level != reportedLevel) {
                reportedLevel = ds.level;
                if (ds.level == DiskLevel::Warning && observer_) {
                    observer_->onWarning("disk-low", "Disk space is running low");
                }
            }
            if (ds.level == DiskLevel::Critical && active) {
                LEC_WARN("capture", "disk almost full ({} MB free) - stopping recording", ds.availableBytes >> 20);
                if (observer_) observer_->onWarning("disk-full", "Disk is almost full; recording was stopped");
                (void)stop(StopReason::DiskFull);
            }
        }

        // Track failures.
        bool allFailed = !tracks_.empty();
        for (Track& t : tracks_) {
            const bool failed = (t.video && t.video->hasFailed()) || (t.audio && t.audio->hasFailed());
            allFailed &= failed;
            if (failed && !t.failureReported) {
                t.failureReported = true;
                const TrackOutcome o = t.video ? t.video->outcome() : t.audio->outcome();
                if (observer_ && o.error) observer_->onTrackFailed(t.plan.trackId, *o.error);
            }
        }
        if (allFailed && active) (void)stop(StopReason::AllTracksFailed);

        // Checkpoint.
        bool requested;
        {
            std::lock_guard lock(mutex_);
            requested = checkpointRequested_;
            checkpointRequested_ = false;
        }
        if ((requested || now - lastCheckpoint >= config_.checkpointInterval) && (active || requested)) {
            lastCheckpoint = now;
            if (state() != SessionState::Stopping) writeCheckpoint();
        }
    }
}

SessionManifest RecordingSession::buildManifest(bool final) const {
    SessionManifest m;
    m.sessionId = config_.sessionId;
    m.title = config_.title;
    m.appVersion = LECTERN_VERSION;
    m.hostClock = hostClockName();
    {
        std::lock_guard lock(mutex_);
        m.state = final ? SessionState::Completed : state_;
        m.stopReason = stopReason_;
        m.createdAtUtc = createdAt_;
    }
    if (final) m.finishedAtUtc = json::utcNowIso8601();
    m.startHostNs = clock_.startHostNs().value_or(0);
    m.stopHostNs = clock_.stopHostNs();
    m.duration = clock_.stopSessionTime().value_or(clock_.elapsed());
    m.checkpointHostNs = hostClock_.nowNs();
    for (const SessionClock::PauseInterval& p : clock_.pauses()) {
        m.pauses.push_back({p.hostBegin, p.hostEnd, p.sessionTime});
    }
    for (const Track& t : tracks_) {
        ManifestTrack mt;
        mt.id = t.plan.trackId;
        mt.role = t.plan.role;
        mt.mediaType = mediaTypeOf(t.plan.role);
        mt.file = t.relativeFile;
        TrackOutcome o;
        if (t.video) {
            const VideoSourceInfo info = t.plan.videoSource->info();
            mt.sourceName = t.plan.displayName.empty() ? info.name : t.plan.displayName;
            mt.deviceId = info.deviceId;
            o = t.video->outcome();
            const VideoTrackStats vs = t.video->stats();
            mt.framesEncoded = vs.framesEncoded;
            mt.framesDuplicated = vs.framesDuplicated;
            mt.framesDropped = vs.droppedQueueFull + t.plan.videoSource->stats().droppedBySource;
        } else {
            const AudioSourceInfo info = t.plan.audioSource->info();
            mt.sourceName = t.plan.displayName.empty() ? info.name : t.plan.displayName;
            mt.deviceId = info.deviceId;
            mt.latencyCompensationNs = info.latencyCompensationNs;
            o = t.audio->outcome();
            const AudioTrackStats as = t.audio->stats();
            mt.driftPpm = as.driftPpm;
            mt.hardCorrections = as.hardCorrections;
        }
        mt.codec = o.codec;
        mt.encoder = o.encoder;
        mt.hardwareEncoder = o.hardwareEncoder;
        mt.width = o.width;
        mt.height = o.height;
        mt.frameRate = o.frameRate;
        mt.sampleRate = o.sampleRate;
        mt.channels = o.channels;
        mt.state = o.state;
        mt.start = o.start;
        mt.end = o.end;
        mt.bytes = final ? o.bytes : (t.video ? t.video->bytesWritten() : t.audio->bytesWritten());
        if (o.error) mt.error = o.error->toString();
        m.tracks.push_back(std::move(mt));
    }
    return m;
}

SessionManifest RecordingSession::manifestSnapshot() const { return buildManifest(false); }

RecordingStats RecordingSession::stats() const {
    RecordingStats s;
    DiskSpaceMonitor::Status disk;
    bool ready;
    {
        std::lock_guard lock(mutex_);
        s.state = state_;
        disk = diskStatus_;
        ready = tracksReady_;
    }
    s.duration = clock_.elapsed();
    s.pauseCount = clock_.pauseCount();
    s.diskLevel = disk.level;
    s.diskAvailableBytes = disk.availableBytes;
    s.writeBytesPerSecond = disk.bytesPerSecond;
    s.diskSecondsRemaining = disk.secondsRemaining;
    if (!ready) return s;
    const std::int64_t now = hostClock_.nowNs();
    for (const Track& t : tracks_) {
        TrackStats ts;
        ts.id = t.plan.trackId;
        ts.role = t.plan.role;
        ts.type = mediaTypeOf(t.plan.role);
        if (t.video) {
            const TrackOutcome o = t.video->outcome();
            ts.state = o.state;
            if (o.error) ts.error = o.error->message();
            ts.sourceName = t.plan.videoSource->info().name;
            ts.bytesWritten = t.video->bytesWritten();
            ts.packetQueueBytes = t.video->packetQueueBytes();
            VideoTrackStats vs = t.video->stats();
            vs.droppedBySource = t.plan.videoSource->stats().droppedBySource;
            ts.video = vs;
        } else {
            const TrackOutcome o = t.audio->outcome();
            ts.state = o.state;
            if (o.error) ts.error = o.error->message();
            ts.sourceName = t.plan.audioSource->info().name;
            ts.bytesWritten = t.audio->bytesWritten();
            ts.packetQueueBytes = t.audio->packetQueueBytes();
            AudioTrackStats as = t.audio->stats();
            const LiveAudioStats live = t.plan.audioSource->stats();
            as.levels = t.plan.audioSource->levels();
            as.overflowFrames = live.overflowFrames;
            as.active = live.lastChunkHostNs != 0 && now - live.lastChunkHostNs < 1'000'000'000;
            ts.audio = as;
        }
        s.totalBytes += ts.bytesWritten;
        s.tracks.push_back(std::move(ts));
    }
    return s;
}

}  // namespace lectern::capture
