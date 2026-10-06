// lectern-export — render a project headlessly: one frame to PNG, or the
// whole edit to MP4, with the same compositor and mixer as the app.

#include "core/Log.h"
#include "editor/Compositor.h"
#include "editor/Exporter.h"
#include "editor/PlatformSegmenter.h"
#ifdef LECTERN_HAS_GPU_RENDERER
#include "render/GpuRenderer.h"
#endif
#include "editor/FrameProvider.h"
#include "editor/RenderPlan.h"
#include "media/FFmpeg.h"
#include "project/ProjectStore.h"

#include <QGuiApplication>
#include <QImage>

#include <cstdio>
#include <cstring>
#include <string>

using namespace lectern;

namespace {

void usage() {
    std::printf(
        "lectern-export — render a Lectern project (%s %s)\n\n"
        "usage:\n"
        "  lectern-export <project-dir> --frame <seconds> --png <file> [--width <px>]\n"
        "  lectern-export <project-dir> --out <file.mp4> [--resolution 720p|1080p|1440p|4K] [--fps <n>]\n"
        "                               [--quality standard|high|max] [--software]\n",
        LECTERN_PRODUCT_NAME, LECTERN_VERSION);
}

}  // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);  // fonts for text and subtitles
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0) {
        usage();
        return argc < 2 ? 2 : 0;
    }
    Logger::instance().addSink(makeStderrSink());
    Logger::instance().setLevel(LogLevel::Warn);
    media::initializeFFmpeg(LogLevel::Error);
    editor::installPlatformSegmenter();  // camera background blur, like the app
#ifdef LECTERN_HAS_GPU_RENDERER
    render::installGpuRenderer();  // preview and export on the GPU (LECTERN_RENDERER=cpu to opt out)
#endif

    const std::filesystem::path dir = argv[1];
    double frame = -1;
    int width = 0;
    std::string png;
    std::string out;
    std::string resolution = "1080p";
    std::string quality = "high";
    int fps = 0;
    bool software = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", arg.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--frame") frame = std::stod(value());
        else if (arg == "--png") png = value();
        else if (arg == "--width") width = std::stoi(value());
        else if (arg == "--out") out = value();
        else if (arg == "--resolution") resolution = value();
        else if (arg == "--fps") fps = std::stoi(value());
        else if (arg == "--quality") quality = value();
        else if (arg == "--software") software = true;
        else {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            usage();
            return 2;
        }
    }

    auto loaded = project::ProjectStore::load(dir);
    if (!loaded) {
        std::fprintf(stderr, "cannot open project: %s\n", loaded.error().toString().c_str());
        return 1;
    }
    const project::Project& project = loaded->project;
    const int canvasW = project.canvas.width;
    const int canvasH = project.canvas.height;

    if (!png.empty()) {
        const int w = (width > 0 ? width : canvasW) & ~1;
        const int h = static_cast<int>(static_cast<double>(w) * canvasH / canvasW) & ~1;
        editor::FrameProvider frames(dir, true);
        frames.setProject(std::make_shared<const project::Project>(project));
        const auto renderer = editor::makeRenderer();
        renderer->setProjectDirectory(dir);
        QImage image(w, h, QImage::Format_RGB32);
        renderer->render(editor::buildRenderPlan(project, Time::fromSecondsF(std::max(0.0, frame))), image,
                          [&frames](const editor::VisualLayer& l, QSizeF box) { return frames.image(l, box); });
        if (!image.save(QString::fromStdString(png))) {
            std::fprintf(stderr, "cannot write %s\n", png.c_str());
            return 1;
        }
        std::printf("frame %.3f s → %s (%dx%d)\n", frame, png.c_str(), w, h);
        return 0;
    }

    if (out.empty()) {
        usage();
        return 2;
    }
    const int shorter = std::max(2, std::min(canvasW, canvasH));
    const int target = resolution == "720p" ? 720 : resolution == "1440p" ? 1440 : resolution == "4K" || resolution == "2160p" ? 2160 : 1080;
    editor::ExportOptions options;
    options.output = out;
    options.width = static_cast<int>(std::lround(canvasW * static_cast<double>(target) / shorter / 2.0)) * 2;
    options.height = static_cast<int>(std::lround(canvasH * static_cast<double>(target) / shorter / 2.0)) * 2;
    options.frameRate = fps > 0 ? FrameRate(fps, 1) : project.canvas.frameRate;
    options.quality = quality;
    options.hardwareEncoder = !software;
    int lastPercent = -1;
    auto result = editor::exportProject(project, dir, options, [&lastPercent](const editor::ExportProgress& p) {
        const int percent = static_cast<int>(p.fraction * 100);
        if (percent / 10 != lastPercent / 10) {
            std::printf("  %3d%%  %.1f× real time\n", percent, p.speed);
            std::fflush(stdout);
        }
        lastPercent = percent;
    });
    if (!result) {
        std::fprintf(stderr, "export failed: %s\n", result.error().toString().c_str());
        return 1;
    }
    std::printf("exported %s: %dx%d, %lld frames, %.1f MB, %s%s, %.1f s\n", out.c_str(), options.width, options.height,
                static_cast<long long>(result->frames), static_cast<double>(result->bytes) / 1e6,
                result->videoEncoder.c_str(), result->hardware ? " (hardware)" : "", result->seconds);
    return 0;
}
