#pragma once

// A small edited-recording project on disk: screen (bluish video), camera
// (reddish video) and microphone (sample-exact ramp) as linked clips with a
// picture-in-picture layout, like RecordingImporter builds them.

#include "project/Project.h"
#include "support/TestMedia.h"
#include "support/TestSupport.h"

#include <QColor>

#include <functional>
#include <memory>

namespace lectern::test {

inline constexpr int kScreenU = 160;
inline constexpr int kScreenV = 110;
inline constexpr int kCameraU = 110;
inline constexpr int kCameraV = 170;

struct EditorFixture {
    TempDir dir{"lectern-editor"};
    project::Project project;
    project::MediaId screen;
    project::MediaId camera;
    project::MediaId mic;
    timeline::ClipId screenClip;
    timeline::ClipId cameraClip;
    timeline::ClipId micClip;

    struct Options {
        double seconds = 3.0;
        bool camera = true;
        std::function<float(double)> micEnvelope;
    };
    EditorFixture();
    explicit EditorFixture(const Options& options);

    [[nodiscard]] std::shared_ptr<const project::Project> snapshot() const {
        return std::make_shared<const project::Project>(project);
    }
    timeline::Track& track(const std::string& name);
    /// Adds an audio file as its own track (e.g. music); returns the clip id.
    timeline::ClipId addAudioTrack(const std::string& name, const TestAudioSpec& spec);
    /// Adds a text clip on a "Text" overlay track.
    timeline::ClipId addText(const std::string& text, double start, double duration, const std::string& preset);
};

/// BT.709 limited-range YUV → RGB (what a correct decoder shows).
[[nodiscard]] QColor yuv709(int y, int u, int v);
/// Whether two colors are within `tolerance` per channel.
[[nodiscard]] bool near(QColor a, QColor b, int tolerance = 12);

}  // namespace lectern::test
