#include "project/Project.h"

#include "core/Json.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <set>
#include <vector>

namespace lectern::project {

std::string_view toString(MediaKind kind) noexcept {
    switch (kind) {
        case MediaKind::Video: return "video";
        case MediaKind::Audio: return "audio";
        case MediaKind::Image: return "image";
    }
    return "video";
}

std::string_view toString(MediaRole role) noexcept {
    switch (role) {
        case MediaRole::Screen: return "screen";
        case MediaRole::Camera: return "camera";
        case MediaRole::Phone: return "phone";
        case MediaRole::Microphone: return "microphone";
        case MediaRole::SystemAudio: return "systemAudio";
        case MediaRole::Imported: return "imported";
    }
    return "imported";
}

std::string layoutKey(std::string_view preset, int canvasWidth, int canvasHeight) {
    const int w = std::max(1, canvasWidth);
    const int h = std::max(1, canvasHeight);
    const int g = std::gcd(w, h);
    return std::string(preset) + '@' + std::to_string(w / g) + ':' + std::to_string(h / g);
}

Project Project::createEmpty(std::string title) {
    Project p;
    p.id = ProjectId::generate();
    p.title = std::move(title);
    p.createdAt = json::utcNowIso8601();
    p.modifiedAt = p.createdAt;
    return p;
}

const MediaSource* Project::findMedia(const MediaId& mediaId) const {
    for (const auto& m : media) {
        if (m.id == mediaId) return &m;
    }
    return nullptr;
}

MediaSource* Project::findMedia(const MediaId& mediaId) {
    for (auto& m : media) {
        if (m.id == mediaId) return &m;
    }
    return nullptr;
}

Status Project::validate() const {
    if (!id.isValid()) return fail(ErrorCode::Corrupt, "project.id: missing");
    if (canvas.width <= 0 || canvas.height <= 0 || canvas.width > 16384 || canvas.height > 16384) {
        return fail(ErrorCode::Corrupt, "project.canvas: invalid size");
    }
    if (!canvas.frameRate.isValid()) return fail(ErrorCode::Corrupt, "project.canvas.frameRate: invalid");
    std::set<Uuid> mediaIds;
    for (std::size_t i = 0; i < media.size(); ++i) {
        if (!media[i].id.isValid() || !mediaIds.insert(media[i].id.uuid()).second) {
            return fail(ErrorCode::Corrupt, "project.media[" + std::to_string(i) + "].id: missing or duplicate");
        }
        if (media[i].path.empty()) return fail(ErrorCode::Corrupt, "project.media[" + std::to_string(i) + "].path: empty");
    }
    std::size_t clips = 0;
    for (const auto& t : timeline.tracks) clips += t.clips.size();
    if (clips > 100'000) return fail(ErrorCode::Corrupt, "project: too many clips");
    return timeline.validate([&](const MediaId& mid) { return mediaIds.contains(mid.uuid()); });
}

Result<Fingerprint> computeFingerprint(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return fail(ErrorCode::NotFound, "fingerprint " + path.string() + ": " + ec.message());
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(ErrorCode::IoError, "open " + path.string());

    constexpr std::uint64_t kChunk = 1u << 20;
    std::uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a 64 offset basis
    auto mix = [&h](const char* data, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            h ^= static_cast<unsigned char>(data[i]);
            h *= 0x100000001b3ull;
        }
    };
    std::vector<char> buf(kChunk);
    const std::uint64_t head = std::min<std::uint64_t>(size, kChunk);
    in.read(buf.data(), static_cast<std::streamsize>(head));
    mix(buf.data(), static_cast<std::size_t>(in.gcount()));
    if (size > kChunk) {
        const std::uint64_t tailStart = std::max<std::uint64_t>(head, size - kChunk);
        in.seekg(static_cast<std::streamoff>(tailStart));
        in.read(buf.data(), static_cast<std::streamsize>(size - tailStart));
        mix(buf.data(), static_cast<std::size_t>(in.gcount()));
    }
    // Fold the size in so files with identical head/tail but different length differ.
    for (int i = 0; i < 8; ++i) {
        h ^= (size >> (i * 8)) & 0xFF;
        h *= 0x100000001b3ull;
    }
    char hex[32];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
    return Fingerprint{size, std::string("fnv1a64:") + hex};
}

}  // namespace lectern::project
