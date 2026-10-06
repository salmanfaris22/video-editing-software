#include "core/Settings.h"

#include "core/FileSystem.h"
#include "core/Json.h"

namespace lectern {

namespace {

json::Json toJson(const AppSettings& s) {
    const auto& r = s.recording;
    return json::Json{
        {"version", s.version},
        {"projectsDirectory", s.projectsDirectory.string()},
        {"hardwareAcceleration", s.hardwareAcceleration},
        {"logLevel", std::string(toString(s.logLevel))},
        {"recording",
         {{"resolution", r.resolution},
          {"frameRate", r.frameRate},
          {"quality", r.quality},
          {"encoder", r.encoder},
          {"captureSystemAudio", r.captureSystemAudio},
          {"showCursor", r.showCursor},
          {"countdown", r.countdown},
          {"countdownSeconds", r.countdownSeconds},
          {"excludeOwnWindows", r.excludeOwnWindows},
          {"lastScreenTarget", r.lastScreenTarget},
          {"lastCameraId", r.lastCameraId},
          {"lastMicrophoneId", r.lastMicrophoneId}}},
    };
}

// Lenient by design: unknown keys are ignored and invalid values fall back to
// defaults, so a hand-edited or older settings file never blocks startup.
AppSettings fromJson(const json::Json& j) {
    AppSettings s;
    auto get = [](const json::Json& obj, const char* key, auto fallback) {
        using T = decltype(fallback);
        if (!obj.is_object()) return fallback;
        const auto it = obj.find(key);
        if (it == obj.end()) return fallback;
        try {
            return it->template get<T>();
        } catch (const nlohmann::json::exception&) {
            return fallback;
        }
    };
    s.version = get(j, "version", s.version);
    s.projectsDirectory = get(j, "projectsDirectory", std::string());
    s.hardwareAcceleration = get(j, "hardwareAcceleration", s.hardwareAcceleration);
    s.logLevel = logLevelFromString(get(j, "logLevel", std::string("info")));
    if (const auto it = j.find("recording"); it != j.end() && it->is_object()) {
        const auto& rj = *it;
        auto& r = s.recording;
        r.resolution = get(rj, "resolution", r.resolution);
        r.frameRate = get(rj, "frameRate", r.frameRate);
        r.quality = get(rj, "quality", r.quality);
        r.encoder = get(rj, "encoder", r.encoder);
        r.captureSystemAudio = get(rj, "captureSystemAudio", r.captureSystemAudio);
        r.showCursor = get(rj, "showCursor", r.showCursor);
        r.countdown = get(rj, "countdown", r.countdown);
        r.countdownSeconds = get(rj, "countdownSeconds", r.countdownSeconds);
        r.excludeOwnWindows = get(rj, "excludeOwnWindows", r.excludeOwnWindows);
        r.lastScreenTarget = get(rj, "lastScreenTarget", r.lastScreenTarget);
        r.lastCameraId = get(rj, "lastCameraId", r.lastCameraId);
        r.lastMicrophoneId = get(rj, "lastMicrophoneId", r.lastMicrophoneId);
        if (r.frameRate != 24 && r.frameRate != 30 && r.frameRate != 60) r.frameRate = 30;
        if (r.countdownSeconds < 0 || r.countdownSeconds > 10) r.countdownSeconds = 3;
    }
    return s;
}

}  // namespace

std::filesystem::path SettingsStore::defaultPath() { return fs::appDataDirectory() / "settings.json"; }

Result<AppSettings> SettingsStore::load() const {
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) return AppSettings{};
    auto text = fs::readFile(path_, 1u << 20);
    if (!text) return fail(std::move(text).error());
    auto parsed = json::parse(*text, path_.string());
    if (!parsed) return fail(std::move(parsed).error());
    return fromJson(*parsed);
}

Status SettingsStore::save(const AppSettings& settings) const {
    return fs::writeFileAtomic(path_, json::dump(toJson(settings)), {.fsync = true, .keepBackup = false});
}

}  // namespace lectern
