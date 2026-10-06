#pragma once

// Draws a RenderPlan with QPainter (docs/RENDERING_PIPELINE.md §4). Used by
// both the preview and the exporter, so they cannot drift apart. CPU raster;
// the reference for the GPU renderer in src/render (FrameRenderer.h).

#include "editor/ColorGrading.h"
#include "editor/FrameRenderer.h"
#include "editor/RenderPlan.h"

#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QSizeF>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

class QPainter;
class QPaintDevice;

namespace lectern::editor {

/// Separates a person from the background: returns an 8-bit mask (255 =
/// person) of any size for an RGB32 image, or a null image when it cannot.
using PersonSegmenter = std::function<QImage(const QImage& image)>;
/// The process-wide segmenter (set by the app on platforms that have one).
void setPersonSegmenter(PersonSegmenter segmenter);
[[nodiscard]] bool hasPersonSegmenter();

/// How a text layer sits on a W×H canvas, before its animation. Shared by
/// drawing and hit-testing so the outline on the preview matches the pixels.
struct TextBlock {
    QFont font;
    QStringList lines;
    double lineHeight = 0;
    double textWidth = 0;
    double padX = 0;
    double padY = 0;
    QRectF block;  ///< canvas pixels
};
[[nodiscard]] TextBlock layoutText(const VisualLayer& layer, double width, double height, QPaintDevice* device);
/// The text block where it is drawn at this instant (animation offset and scale applied).
[[nodiscard]] QRectF animatedTextRect(const VisualLayer& layer, const TextBlock& text, double width, double height);
/// The subtitle box on a W×H canvas, moved above any of `avoid` (titles on screen).
[[nodiscard]] QRectF subtitleRect(const VisualLayer& layer, const project::StyleSettings& style, double width, double height,
                                  const std::vector<QRectF>& avoid, QPaintDevice* device);

class Compositor {
public:
    using ImageSource = FrameRenderer::ImageSource;

    /// The project folder LUT paths are relative to.
    void setProjectDirectory(std::filesystem::path dir) { projectDir_ = std::move(dir); }

    /// Renders `plan` into `target`, scaling the canvas to the target's size.
    void render(const RenderPlan& plan, QImage& target, const ImageSource& images);

    /// Draws a text layer; returns the area it occupies (the GPU renderer
    /// rasterizes text with these too, so both renderers lay text out alike).
    static QRectF drawText(QPainter& p, const VisualLayer& layer, double width, double height);
    /// Moves up above any of `avoid` it would cover (titles drawn this frame).
    static void drawSubtitle(QPainter& p, const VisualLayer& layer, const project::StyleSettings& style, double width,
                             double height, const std::vector<QRectF>& avoid);
    /// The person mask (Grayscale8, image size, 255 = person) from the
    /// process-wide segmenter; null without one.
    [[nodiscard]] static QImage segmentPerson(const QImage& image);

private:
    void drawMedia(QPainter& p, const VisualLayer& layer, double width, double height, const ImageSource& images);
    /// `opaque`: the layer drawn on top fully covers its interior.
    void drawShadow(QPainter& p, const QRectF& rect, const VisualLayer& layer, double height, bool opaque);
    /// Blurs what is behind the person in `image` (camera background blur).
    void blurBackground(QImage& image, const VisualLayer& layer, double height);
    /// The person mask of a layer image (Grayscale8, same size), cached per
    /// frame; null without a segmenter.
    QImage personMask(const QImage& image, const VisualLayer& layer);

    std::map<std::tuple<int, int, int, bool>, QImage> shadowCache_;  ///< full-resolution shadows by shape
    QImage background_;  ///< the canvas background, rebuilt only when it changes
    QString backgroundKey_;
    std::filesystem::path projectDir_;
    LutCache luts_;
    struct MaskEntry {
        std::string key;
        QImage mask;
    };
    std::vector<MaskEntry> masks_;  ///< recent person masks (paused previews redraw the same frame)
};

/// Blurs in place (≈ Gaussian with the given radius in pixels; large radii run at reduced resolution).
void blurImage(QImage& image, double radiusPixels);
/// Outline of a media layer's shape (rectangle, rounded rectangle or circle).
[[nodiscard]] QPainterPath layerShape(const QRectF& rect, const VisualLayer& layer, double canvasHeight);

}  // namespace lectern::editor
