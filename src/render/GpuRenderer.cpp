#include "render/GpuRenderer.h"

#include "core/Log.h"
#include "editor/ColorGrading.h"
#include "editor/Compositor.h"

#include <QFile>
#include <QMatrix4x4>
#include <QPainter>
#include <QtCore/qfloat16.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

namespace lectern::render {

namespace {

using editor::LayerKind;
using editor::RenderPlan;
using editor::VisualLayer;

// Must match the uniform block in shaders/quad.vert and shaders/layer.frag (std140).
struct Uniforms {
    float mvp[16];
    float quad[4];
    float shape[4];
    float uvMap[4];
    float look[4];
    float grade[4];
    float lutScale[4];
    float lutOffset[4];
    float color0[4];
    float color1[4];
    float blurDir[4];
    float inputColor[4];
    float gamut0[4];
    float gamut1[4];
    float gamut2[4];
    float grade2[4];
    float fx[4];         // grain amount, grain size (px), grain seed; threshold modes: threshold
    float nodeInfo[4];   // node count, source aspect, person mask bound, highlighted node
    float nodes[70][4];  // 7 per node (layer.frag "Nodes")
};
static_assert(sizeof(Uniforms) == 1456);
constexpr std::size_t kNodeVecs = 7;
constexpr std::size_t kMaxRenderNodes = 10;  ///< a clip's 8 nodes, film emulation, one spare
constexpr int kCurveRows = 1 + static_cast<int>(kMaxRenderNodes);

enum Mode {
    kGradient = 0,
    kMedia = 1,
    kShadow = 2,
    kSolid = 3,
    kOverlay = 4,
    kPrepare = 5,
    kBlur = 6,
    kMaskMix = 7,
    kNv12Y = 8,
    kNv12Uv = 9,
    kThreshold = 10,
    kAdd = 11
};

void set4(float* dst, double a, double b, double c, double d) {
    dst[0] = static_cast<float>(a);
    dst[1] = static_cast<float>(b);
    dst[2] = static_cast<float>(c);
    dst[3] = static_cast<float>(d);
}

QColor parseColor(const std::string& text, QColor fallback) {
    const QColor c(QString::fromStdString(text));
    return c.isValid() ? c : fallback;
}

QShader loadShader(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

std::unique_ptr<QRhi> createRhi() {
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    QRhiMetalInitParams params;
    return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
#elif defined(Q_OS_WIN)
    QRhiD3D11InitParams params;
    return std::unique_ptr<QRhi>(QRhi::create(QRhi::D3D11, &params));
#else
    return nullptr;
#endif
}

/// Effective Gaussian sigma of editor::blurImage(radius) (three box passes).
double blurSigma(double radius) { return radius * 0.6; }

/// What identifies the pixels of a run of text/subtitle layers.
std::string textKey(const std::vector<const VisualLayer*>& run, const RenderPlan& plan, const std::vector<QRectF>& titles,
                    int W, int H) {
    std::ostringstream k;
    k.precision(9);
    k << W << 'x' << H << '|' << plan.style.subtitleSize << plan.style.subtitleColor << plan.style.subtitleBackground << '|';
    for (const QRectF& r : titles) k << r.x() << ',' << r.y() << ',' << r.width() << ',' << r.height() << ';';
    for (const VisualLayer* l : run) {
        const timeline::TextStyle& s = l->textStyle;
        k << '#' << static_cast<int>(l->kind) << l->text << '\x1f' << l->textPreset << s.font << s.size << s.weight << s.color
          << s.alignment << s.lineSpacing << s.letterSpacing << s.background << s.shadowBlur << s.shadowColor << l->anchorX
          << l->anchorY << l->textDx << l->textDy << l->textScale << l->textReveal << l->textBlur << l->textChars
          << l->opacity;
    }
    return k.str();
}

class GpuRenderer final : public editor::FrameRenderer {
public:
    void setProjectDirectory(std::filesystem::path dir) override {
        projectDir_ = dir;
        cpu_->setProjectDirectory(std::move(dir));
    }
    [[nodiscard]] std::string name() const override {
        return rhi_ && !failed_ ? std::string("gpu (") + rhi_->backendName() + ")" : std::string("cpu (gpu unavailable)");
    }
    bool renderNv12(const RenderPlan& plan, QSize size, std::uint8_t* y, int yStride, std::uint8_t* uv, int uvStride,
                    const ImageSource& images) override {
        if (failed_ || (!rhi_ && !init()) || size.width() % 2 || size.height() % 2) return false;
        if (!nv12Supported()) return false;
        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        std::vector<Pass> passes = buildFrame(plan, size, images);
        const auto t1 = Clock::now();
        const QSize half(size.width() / 2, size.height() / 2);
        if (!lumaTarget_ || lumaTarget_->size != size) lumaTarget_ = makeTarget(size, QRhiTexture::R8);
        if (!chromaTarget_ || chromaTarget_->size != half) chromaTarget_ = makeTarget(half, QRhiTexture::RG8);
        if (!lumaPipeline_) lumaPipeline_ = makePipeline(vs_, fs_, false, lumaTarget_.get());
        if (!chromaPipeline_) chromaPipeline_ = makePipeline(vs_, fs_, false, chromaTarget_.get());
        Draw dy = draw(projection(size), QRectF(QPointF(0, 0), QSizeF(size)), kNv12Y);
        set4(dy.u.shape, size.width(), size.height(), 0, 0);
        dy.tex = canvas_->texture.get();
        dy.pipeline = lumaPipeline_.get();
        passes.push_back({lumaTarget_.get(), Qt::black, {dy}});
        Draw duv = draw(projection(half), QRectF(QPointF(0, 0), QSizeF(half)), kNv12Uv);
        set4(duv.u.shape, half.width(), half.height(), 0, 0);
        duv.tex = canvas_->texture.get();
        duv.pipeline = chromaPipeline_.get();
        passes.push_back({chromaTarget_.get(), Qt::black, {duv}});
        QRhiReadbackResult luma;
        QRhiReadbackResult chroma;
        if (!execute(passes, {{lumaTarget_->texture.get(), &luma}, {chromaTarget_->texture.get(), &chroma}})) {
            failed_ = true;
            return false;
        }
        if (luma.data.size() < static_cast<qsizetype>(size.width()) * size.height() ||
            chroma.data.size() < static_cast<qsizetype>(half.width()) * half.height() * 2) {
            return false;
        }
        const bool flip = rhi_->isYUpInFramebuffer();
        auto copyPlane = [flip](const QByteArray& data, int rowBytes, int rows, std::uint8_t* dst, int dstStride) {
            const qsizetype srcStride = data.size() / rows;  // rows may be padded
            for (int r = 0; r < rows; ++r) {
                std::memcpy(dst + static_cast<std::ptrdiff_t>(r) * dstStride,
                            data.constData() + (flip ? rows - 1 - r : r) * srcStride, static_cast<std::size_t>(rowBytes));
            }
        };
        copyPlane(luma.data, size.width(), size.height(), y, yStride);
        copyPlane(chroma.data, half.width() * 2, half.height(), uv, uvStride);
        if (profile_) {
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            std::fprintf(stderr, "render nv12: decode+prepare %.2f ms, gpu+readback+copy %.2f ms\n", ms(t0, t1),
                         ms(t1, Clock::now()));
        }
        return true;
    }

    void render(const RenderPlan& plan, QImage& target, const ImageSource& images) override {
        if (target.isNull()) return;
        if (!failed_ && (rhi_ || init())) {
            if (renderGpu(plan, target, images)) return;
            LEC_WARN("render", "GPU frame failed; using the CPU renderer from now on");
            failed_ = true;
        }
        cpu_->render(plan, target, images);
    }

    bool init() {
        rhi_ = createRhi();
        if (!rhi_) {
            failed_ = true;
            return false;
        }
        format_ = rhi_->isTextureFormatSupported(QRhiTexture::BGRA8) ? QRhiTexture::BGRA8 : QRhiTexture::RGBA8;
        const QShader vs = loadShader(QStringLiteral(":/lectern/render/shaders/quad.vert.qsb"));
        const QShader fs = loadShader(QStringLiteral(":/lectern/render/shaders/layer.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            LEC_ERROR("render", "GPU shaders are missing from the build");
            rhi_.reset();
            failed_ = true;
            return false;
        }
        static const float quad[] = {0, 0, 0, 1, 1, 0, 1, 1};
        vbuf_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(quad)));
        vbuf_->create();
        linear_.reset(rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                       QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        linear_->create();
        nearest_.reset(rhi_->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        nearest_->create();
        dummy2D_.reset(rhi_->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        dummy2D_->create();
        dummy3D_.reset(rhi_->newTexture(QRhiTexture::RGBA16F, 1, 1, 1, 1, QRhiTexture::ThreeDimensional));
        dummy3D_->create();
        ubuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uboStride() * 16));
        ubuf_->create();
        uboCapacity_ = 16;

        // A tiny render target whose pass descriptor the pipelines are built
        // with; every other target has the same format, so it is compatible.
        templateTarget_ = makeTarget(QSize(8, 8));
        templateSrb_.reset(rhi_->newShaderResourceBindings());
        const auto initial = bindings(0, dummy2D_.get(), nullptr, nullptr, nullptr, nullptr);
        templateSrb_->setBindings(initial.cbegin(), initial.cend());
        templateSrb_->create();
        blendPipeline_ = makePipeline(vs, fs, true);
        copyPipeline_ = makePipeline(vs, fs, false);
        vs_ = vs;
        fs_ = fs;

        pending_ = rhi_->nextResourceUpdateBatch();
        pending_->uploadStaticBuffer(vbuf_.get(), quad);
        QImage white(1, 1, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        pending_->uploadTexture(dummy2D_.get(), white);
        LEC_INFO("render", "GPU renderer ready ({})", rhi_->backendName());
        return true;
    }

private:
    struct Target {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        QSize size;
        bool busy = false;
    };
    struct Draw {
        Uniforms u{};
        QRhiTexture* tex = nullptr;
        QRhiTexture* curves = nullptr;
        QRhiTexture* lut = nullptr;
        QRhiTexture* aux = nullptr;
        QRhiTexture* mask = nullptr;
        bool blend = true;
        QRhiGraphicsPipeline* pipeline = nullptr;  ///< overrides blend (other target formats)
    };
    struct Pass {
        Target* target = nullptr;
        QColor clear;
        std::vector<Draw> draws;
    };
    struct SourceTexture {
        std::unique_ptr<QRhiTexture> texture;
        QSize size;
        qint64 key = -1;
        std::unique_ptr<QRhiTexture> curves;
        std::vector<editor::ColorParams> curveParams;  ///< primary, then each node
        bool curvesValid = false;
    };
    struct LutTexture {
        std::unique_ptr<QRhiTexture> texture;
        std::array<float, 3> scale{};
        std::array<float, 3> offset{};
    };
    struct Overlay {
        std::unique_ptr<QRhiTexture> texture;
        QSize size;
        std::string key;
    };
    struct MaskEntry {
        std::string key;
        std::unique_ptr<QRhiTexture> texture;
    };

    [[nodiscard]] bool nv12Supported() const {
        return rhi_->isTextureFormatSupported(QRhiTexture::R8) && rhi_->isTextureFormatSupported(QRhiTexture::RG8);
    }

    [[nodiscard]] int uboStride() const { return rhi_->ubufAligned(static_cast<int>(sizeof(Uniforms))); }

    QList<QRhiShaderResourceBinding> bindings(int slot, QRhiTexture* tex, QRhiTexture* curves, QRhiTexture* lut,
                                              QRhiTexture* aux, QRhiTexture* mask) {
        const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        const auto fs = QRhiShaderResourceBinding::FragmentStage;
        return {QRhiShaderResourceBinding::uniformBuffer(0, stages, ubuf_.get(), slot * uboStride(), sizeof(Uniforms)),
                QRhiShaderResourceBinding::sampledTexture(1, fs, tex ? tex : dummy2D_.get(), linear_.get()),
                QRhiShaderResourceBinding::sampledTexture(2, fs, curves ? curves : dummy2D_.get(), nearest_.get()),
                QRhiShaderResourceBinding::sampledTexture(3, fs, lut ? lut : dummy3D_.get(), linear_.get()),
                QRhiShaderResourceBinding::sampledTexture(4, fs, aux ? aux : dummy2D_.get(), linear_.get()),
                QRhiShaderResourceBinding::sampledTexture(5, fs, mask ? mask : dummy2D_.get(), linear_.get())};
    }

    std::unique_ptr<QRhiGraphicsPipeline> makePipeline(const QShader& vs, const QShader& fs, bool blend,
                                                       Target* compatible = nullptr) {
        std::unique_ptr<QRhiGraphicsPipeline> ps(rhi_->newGraphicsPipeline());
        ps->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{2 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
        ps->setVertexInputLayout(layout);
        ps->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        if (blend) {  // premultiplied "over"
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = QRhiGraphicsPipeline::One;
            b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            b.srcAlpha = QRhiGraphicsPipeline::One;
            b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            ps->setTargetBlends({b});
        }
        ps->setShaderResourceBindings(templateSrb_.get());
        ps->setRenderPassDescriptor((compatible ? compatible : templateTarget_.get())->pass.get());
        ps->create();
        return ps;
    }

    std::unique_ptr<Target> makeTarget(QSize size, QRhiTexture::Format format = QRhiTexture::UnknownFormat) {
        auto t = std::make_unique<Target>();
        t->size = size;
        t->texture.reset(rhi_->newTexture(format == QRhiTexture::UnknownFormat ? format_ : format, size, 1,
                                          QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        t->texture->create();
        t->target.reset(rhi_->newTextureRenderTarget({QRhiColorAttachment(t->texture.get())}));
        t->pass.reset(t->target->newCompatibleRenderPassDescriptor());
        t->target->setRenderPassDescriptor(t->pass.get());
        t->target->create();
        return t;
    }

    /// A free intermediate target of `size` for this frame.
    Target* acquire(QSize size) {
        for (auto& t : pool_) {
            if (!t->busy && t->size == size) {
                t->busy = true;
                return t.get();
            }
        }
        if (pool_.size() > 24) {  // sizes changed a lot: drop the idle ones
            std::erase_if(pool_, [](const auto& t) { return !t->busy; });
        }
        pool_.push_back(makeTarget(size));
        pool_.back()->busy = true;
        return pool_.back().get();
    }

    QMatrix4x4 projection(QSize size) const {
        QMatrix4x4 m = rhi_->clipSpaceCorrMatrix();
        m.ortho(0, static_cast<float>(size.width()), static_cast<float>(size.height()), 0, -1, 1);
        return m;
    }

    static Draw draw(const QMatrix4x4& mvp, const QRectF& quad, Mode mode) {
        Draw d;
        std::memcpy(d.u.mvp, mvp.constData(), sizeof(d.u.mvp));
        set4(d.u.quad, quad.x(), quad.y(), quad.width(), quad.height());
        set4(d.u.look, 1, 0, 0, mode);
        set4(d.u.grade, 1, 0, 0, 0);
        return d;
    }

    // ---- Resources -----------------------------------------------------------

    /// Uploads `image` (RGB32 / ARGB32 premultiplied, or 10-bit BGR30) into the slot's texture when it changed.
    QRhiTexture* sourceTexture(std::size_t slot, QImage image) {
        if (sources_.size() <= slot) sources_.resize(slot + 1);
        SourceTexture& s = sources_[slot];
        const bool deep = image.format() == QImage::Format_BGR30 && rhi_->isTextureFormatSupported(QRhiTexture::RGB10A2);
        if (!deep && image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32_Premultiplied) {
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        if (!deep && format_ == QRhiTexture::RGBA8) image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        const QRhiTexture::Format format = deep ? QRhiTexture::RGB10A2 : format_;
        if (!s.texture || s.size != image.size() || s.texture->format() != format) {
            s.texture.reset(rhi_->newTexture(format, image.size()));
            s.texture->create();
            s.size = image.size();
            s.key = -1;
        }
        if (s.key != image.cacheKey()) {
            pending_->uploadTexture(s.texture.get(), image);
            s.key = image.cacheKey();
        }
        return s.texture.get();
    }

    /// Row 0: the layer's primary grade; row n + 1: node n (identity when unused).
    QRhiTexture* curvesTexture(std::size_t slot, const editor::ColorParams& color, const std::vector<editor::NodeParams>& nodes) {
        SourceTexture& s = sources_[slot];
        if (!s.curves) {
            s.curves.reset(rhi_->newTexture(QRhiTexture::RGBA8, QSize(256, kCurveRows)));
            s.curves->create();
        }
        std::vector<editor::ColorParams> key{color};
        for (const auto& n : nodes) key.push_back(n.grade);
        if (!s.curvesValid || s.curveParams != key) {
            QImage table(256, kCurveRows, QImage::Format_RGBA8888);
            for (int row = 0; row < kCurveRows; ++row) {
                const editor::ColorParams params = row < static_cast<int>(key.size()) ? key[static_cast<std::size_t>(row)] : editor::ColorParams{};
                const editor::ColorCurves c = editor::colorCurves(params);
                auto* px = table.scanLine(row);
                for (int v = 0; v < 256; ++v) {
                    px[v * 4 + 0] = c[0][static_cast<std::size_t>(v)];
                    px[v * 4 + 1] = c[1][static_cast<std::size_t>(v)];
                    px[v * 4 + 2] = c[2][static_cast<std::size_t>(v)];
                    px[v * 4 + 3] = 255;
                }
            }
            pending_->uploadTexture(s.curves.get(), table);
            s.curveParams = std::move(key);
            s.curvesValid = true;
        }
        return s.curves.get();
    }

    /// Packs a layer's nodes into the uniforms (layer.frag "Nodes").
    static void setNodes(Uniforms& u, const VisualLayer& l, double aspect, bool hasMask) {
        const std::size_t count = std::min(l.nodes.size(), kMaxRenderNodes);
        set4(u.nodeInfo, static_cast<double>(count), aspect, hasMask ? 1 : 0, l.highlightNode);
        constexpr double kPi = 3.14159265358979323846;
        for (std::size_t n = 0; n < count; ++n) {
            const editor::NodeParams& node = l.nodes[n];
            float(*v)[4] = &u.nodes[n * kNodeVecs];
            const auto& w = node.window;
            const auto& q = node.qualifier;
            const int shape = w.shape == "circle" ? 1 : w.shape == "rectangle" ? 2 : w.shape == "gradient" ? 3 : 0;
            set4(v[0], 1.0 + node.grade.saturation, node.grade.colorBoost, node.grade.hue * kPi, 0);
            set4(v[1], shape, w.x, w.y, w.rotation * kPi / 180.0);
            set4(v[2], w.width, w.height, w.softness, w.invert ? 1 : 0);
            set4(v[3], q.enabled ? 1 : 0, q.hue, q.hueWidth, q.hueSoft);
            set4(v[4], q.satLow, q.satHigh, q.satSoft, q.invert ? 1 : 0);
            set4(v[5], q.lumLow, q.lumHigh, q.lumSoft, node.subject);
            set4(v[6], node.invert ? 1 : 0, 0, 0, 0);
        }
    }

    const LutTexture* lutTexture(const std::string& ref) {
        auto it = luts3D_.find(ref);
        if (it != luts3D_.end()) return it->second.texture ? &it->second : nullptr;
        LutTexture& out = luts3D_[ref];
        const std::shared_ptr<const editor::Lut3D> lut = luts_.get(ref, projectDir_);
        if (!lut || lut->size < 2) return nullptr;
        const int n = lut->size;
        out.texture.reset(rhi_->newTexture(QRhiTexture::RGBA16F, n, n, n, 1, QRhiTexture::ThreeDimensional));
        out.texture->create();
        QList<QRhiTextureUploadEntry> slices;
        std::vector<QByteArray> data(static_cast<std::size_t>(n));
        for (int b = 0; b < n; ++b) {
            QByteArray& bytes = data[static_cast<std::size_t>(b)];
            bytes.resize(static_cast<qsizetype>(n) * n * 4 * sizeof(qfloat16));
            auto* h = reinterpret_cast<qfloat16*>(bytes.data());
            for (int g = 0; g < n; ++g) {
                for (int r = 0; r < n; ++r) {
                    const std::size_t i = (static_cast<std::size_t>(b) * n * n + static_cast<std::size_t>(g) * n + r) * 3;
                    const std::size_t o = (static_cast<std::size_t>(g) * n + r) * 4;
                    h[o + 0] = qfloat16(lut->table[i + 0]);
                    h[o + 1] = qfloat16(lut->table[i + 1]);
                    h[o + 2] = qfloat16(lut->table[i + 2]);
                    h[o + 3] = qfloat16(1.0f);
                }
            }
            slices.append(QRhiTextureUploadEntry(b, 0, QRhiTextureSubresourceUploadDescription(bytes)));
        }
        QRhiTextureUploadDescription desc;
        desc.setEntries(slices.cbegin(), slices.cend());
        pending_->uploadTexture(out.texture.get(), desc);
        for (int c = 0; c < 3; ++c) {
            const float range = std::max(1e-6f, lut->domainMax[static_cast<std::size_t>(c)] - lut->domainMin[static_cast<std::size_t>(c)]);
            const float s = static_cast<float>(n - 1) / static_cast<float>(n) / range;
            out.scale[static_cast<std::size_t>(c)] = s;
            out.offset[static_cast<std::size_t>(c)] = 0.5f / static_cast<float>(n) - lut->domainMin[static_cast<std::size_t>(c)] * s;
        }
        return &out;
    }

    /// The person mask of a source image (segmented on the CPU, cached).
    QRhiTexture* personMask(const VisualLayer& l, const QImage& src) {
        if (!editor::hasPersonSegmenter()) return nullptr;
        const std::string key = l.media.toString() + '@' + std::to_string(l.sourceTime.ticks()) + '#' +
                                std::to_string(src.width()) + 'x' + std::to_string(src.height());
        for (const MaskEntry& m : masks_) {
            if (m.key == key) return m.texture.get();
        }
        QImage mask = editor::Compositor::segmentPerson(src);
        if (mask.isNull()) return nullptr;
        mask = mask.convertToFormat(QImage::Format_RGBA8888);  // red = person
        MaskEntry e{key, std::unique_ptr<QRhiTexture>(rhi_->newTexture(QRhiTexture::RGBA8, mask.size()))};
        e.texture->create();
        pending_->uploadTexture(e.texture.get(), mask);
        if (masks_.size() >= 3) masks_.erase(masks_.begin());
        masks_.push_back(std::move(e));
        return masks_.back().texture.get();
    }

    QRhiTexture* overlayTexture(std::size_t index, const std::vector<const VisualLayer*>& run, const RenderPlan& plan,
                                std::vector<QRectF>& titles, QSize size) {
        if (overlays_.size() <= index) overlays_.resize(index + 1);
        Overlay& o = overlays_[index];
        const std::string key = textKey(run, plan, titles, size.width(), size.height());
        // Titles drawn by this run (subtitles in later runs step above them).
        QImage image;
        if (o.key != key || o.size != size || !o.texture) {
            image = QImage(size, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
        }
        {
            QImage scratch(1, 1, QImage::Format_ARGB32_Premultiplied);
            QPainter p(image.isNull() ? &scratch : &image);
            p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
            const double W = size.width();
            const double H = size.height();
            for (const VisualLayer* l : run) {
                if (l->kind == LayerKind::Text) {
                    if (image.isNull()) {
                        // Unchanged pixels: only recompute where the title sits.
                        titles.push_back(l->opacity > 0.01 && l->textChars != 0 && l->textReveal > 0
                                             ? editor::animatedTextRect(*l, editor::layoutText(*l, W, H, &scratch), W, H)
                                             : QRectF());
                    } else {
                        titles.push_back(editor::Compositor::drawText(p, *l, W, H));
                    }
                } else if (!image.isNull()) {
                    editor::Compositor::drawSubtitle(p, *l, plan.style, W, H, titles);
                }
            }
        }
        if (!image.isNull()) {
            if (format_ == QRhiTexture::RGBA8) image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            if (!o.texture || o.size != size) {
                o.texture.reset(rhi_->newTexture(format_, size));
                o.texture->create();
                o.size = size;
            }
            pending_->uploadTexture(o.texture.get(), image);
            o.key = key;
        }
        return o.texture.get();
    }

    // ---- Frame -------------------------------------------------------------------

    /// Blurs `src` (size `size`) with a separable Gaussian; returns the result target.
    Target* blur(std::vector<Pass>& passes, QRhiTexture* src, QSize size, double radiusPx) {
        const double sigma = std::max(0.3, blurSigma(radiusPx));
        // Up to 48 taps per side; wider blurs sample every few texels.
        const double stride = std::max(1.0, std::ceil(3.0 * sigma / 48.0));
        const int taps = static_cast<int>(std::ceil(3.0 * sigma / stride));
        const QMatrix4x4 mvp = projection(size);
        Target* h = acquire(size);
        Target* v = acquire(size);
        Draw dh = draw(mvp, QRectF(QPointF(0, 0), QSizeF(size)), kBlur);
        set4(dh.u.shape, size.width(), size.height(), 0, 0);
        set4(dh.u.blurDir, stride / size.width(), 0, sigma / stride, taps);
        dh.tex = src;
        dh.blend = false;
        passes.push_back({h, Qt::transparent, {dh}});
        Draw dv = dh;
        set4(dv.u.blurDir, 0, stride / size.height(), sigma / stride, taps);
        dv.tex = h->texture.get();
        passes.push_back({v, Qt::transparent, {dv}});
        return v;
    }

    void addMedia(std::vector<Pass>& passes, std::vector<Draw>& main, const QMatrix4x4& canvasProj, const VisualLayer& l,
                  double W, double H, const ImageSource& images, std::size_t slot) {
        const QRectF box(l.box.x * W, l.box.y * H, l.box.w * W, l.box.h * H);
        if (box.width() < 1 || box.height() < 1 || l.opacity <= 0) return;
        const QImage src = images ? images(l, box.size()) : QImage();
        if (src.isNull()) {  // missing media: placeholder in the layer's shape
            QMatrix4x4 m = canvasProj;
            m.translate(static_cast<float>(box.x()), static_cast<float>(box.y()));
            Draw d = draw(m, box.translated(-box.topLeft()).adjusted(-1, -1, 1, 1), kSolid);
            set4(d.u.shape, box.width(), box.height(), l.radius * H, l.circle ? 1 : 0);
            set4(d.u.look, l.opacity, 0, 0, kSolid);
            set4(d.u.color0, 30 / 255.0, 33 / 255.0, 40 / 255.0, 1);
            main.push_back(d);
            return;
        }

        // Geometry: identical to Compositor::drawMedia.
        QRectF srcRect(0, 0, src.width(), src.height());
        if (l.cropL > 0 || l.cropT > 0 || l.cropR > 0 || l.cropB > 0) {
            const double w = srcRect.width();
            const double h = srcRect.height();
            srcRect = QRectF(w * l.cropL, h * l.cropT, w * (1.0 - l.cropL - l.cropR), h * (1.0 - l.cropT - l.cropB));
        }
        if (l.zoom > 1.0001) {
            const double w = srcRect.width() / l.zoom;
            const double h = srcRect.height() / l.zoom;
            const double cx = std::clamp(l.zoomX * src.width(), w / 2, src.width() - w / 2);
            const double cy = std::clamp(l.zoomY * src.height(), h / 2, src.height() - h / 2);
            srcRect = QRectF(cx - w / 2, cy - h / 2, w, h);
        }
        QRectF dest = box;
        if (l.fill) {
            const double boxAspect = box.width() / box.height();
            const double srcAspect = srcRect.width() / srcRect.height();
            if (srcAspect > boxAspect) {
                const double w = srcRect.height() * boxAspect;
                srcRect.setX(srcRect.x() + (srcRect.width() - w) / 2);
                srcRect.setWidth(w);
            } else {
                const double h = srcRect.width() / boxAspect;
                srcRect.setY(srcRect.y() + (srcRect.height() - h) / 2);
                srcRect.setHeight(h);
            }
        } else {
            const double scale = std::min(box.width() / srcRect.width(), box.height() / srcRect.height());
            dest = QRectF(0, 0, srcRect.width() * scale, srcRect.height() * scale);
            dest.moveCenter(box.center());
        }
        QSize size(std::max(1, static_cast<int>(std::lround(dest.width()))),
                   std::max(1, static_cast<int>(std::lround(dest.height()))));
        const QRect srcPixels = srcRect.toAlignedRect().intersected(src.rect());
        if (srcPixels.isEmpty()) return;
        if (std::abs(srcPixels.width() - size.width()) <= 2 && std::abs(srcPixels.height() - size.height()) <= 2) {
            size = srcPixels.size();
        }
        const QRect destPixels(QPoint(static_cast<int>(std::lround(dest.center().x() - size.width() / 2.0)),
                                      static_cast<int>(std::lround(dest.center().y() - size.height() / 2.0))),
                               size);

        QRhiTexture* tex = sourceTexture(slot, src);
        const double texW = src.width();
        const double texH = src.height();
        const bool grade = !l.color.isIdentity();
        const bool primaryCurves = grade && !l.color.curvesAreIdentity();
        QRhiTexture* curves = primaryCurves || !l.nodes.empty() ? curvesTexture(slot, l.color, l.nodes) : nullptr;
        const bool subjects = std::any_of(l.nodes.begin(), l.nodes.end(), [](const editor::NodeParams& n) { return n.subject != 0; });
        // One person mask serves the background blur and person/background nodes.
        QRhiTexture* personTex = l.backgroundBlur > 0 || subjects ? personMask(l, src) : nullptr;
        QRhiTexture* nodeMask = subjects ? personTex : nullptr;
        QRhiTexture* mask = l.backgroundBlur > 0 ? personTex : nullptr;
        const LutTexture* lut = grade && !l.color.lut.empty() && l.color.lutAmount > 0 ? lutTexture(l.color.lut) : nullptr;
        auto setGrade = [&](Draw& d) {
            set4(d.u.uvMap, srcPixels.x() / texW, srcPixels.y() / texH, srcPixels.width() / (texW * size.width()),
                 srcPixels.height() / (texH * size.height()));
            set4(d.u.grade, 1.0 + (grade ? l.color.saturation : 0.0), primaryCurves ? 1 : 0, l.mirror ? 1 : 0, 0);
            set4(d.u.grade2, grade ? l.color.colorBoost : 0.0, grade ? l.color.hue * 3.14159265358979323846 : 0.0, 0, 0);
            if (lut) {
                set4(d.u.lutScale, lut->scale[0], lut->scale[1], lut->scale[2], std::clamp(l.color.lutAmount, 0.0, 1.0));
                set4(d.u.lutOffset, lut->offset[0], lut->offset[1], lut->offset[2], 0);
                d.lut = lut->texture.get();
            }
            d.tex = tex;
            d.curves = curves;
            const bool convert = !l.input.isIdentity();
            set4(d.u.inputColor, static_cast<int>(l.input.transfer), src.format() == QImage::Format_BGR30 ? 1 : 0, convert ? 1 : 0, 0);
            const editor::Matrix3 g = editor::gamutToBt709(l.input.primaries);
            set4(d.u.gamut0, g[0][0], g[0][1], g[0][2], 0);
            set4(d.u.gamut1, g[1][0], g[1][1], g[1][2], 0);
            set4(d.u.gamut2, g[2][0], g[2][1], g[2][2], 0);
            setNodes(d.u, l, texW / texH, nodeMask != nullptr);
            if (nodeMask) d.mask = nodeMask;
        };

        Draw layer = draw({}, {}, kMedia);
        QMatrix4x4 model = canvasProj;
        model.translate(static_cast<float>(destPixels.x()), static_cast<float>(destPixels.y()));
        if (std::abs(l.rotation) > 0.01) {
            model.translate(size.width() / 2.0f, size.height() / 2.0f);
            model.rotate(static_cast<float>(l.rotation), 0, 0, 1);
            model.translate(-size.width() / 2.0f, -size.height() / 2.0f);
        }

        const bool prepare = l.blur > 0 || mask || l.glow > 0 || l.halation > 0;
        if (prepare) {
            // Grade into a layer-sized image, then soften it on the GPU.
            const QMatrix4x4 proj = projection(size);
            Target* graded = acquire(size);
            Draw d = draw(proj, QRectF(QPointF(0, 0), QSizeF(size)), kPrepare);
            set4(d.u.shape, size.width(), size.height(), 0, 0);
            setGrade(d);
            d.blend = false;
            passes.push_back({graded, Qt::transparent, {d}});
            QRhiTexture* current = graded->texture.get();
            // Glow and halation: bright parts, blurred, tinted, added (editor::addGlow).
            auto glowPass = [&](double amount, double threshold, double radiusPx, const std::array<double, 3>& tint) {
                Target* bright = acquire(size);
                Draw t = draw(proj, QRectF(QPointF(0, 0), QSizeF(size)), kThreshold);
                set4(t.u.shape, size.width(), size.height(), 0, 0);
                set4(t.u.fx, std::clamp(threshold, 0.0, 0.98), 0, 0, 0);
                t.tex = current;
                t.blend = false;
                passes.push_back({bright, Qt::transparent, {t}});
                Target* soft = blur(passes, bright->texture.get(), size, radiusPx);
                Target* sum = acquire(size);
                Draw a = draw(proj, QRectF(QPointF(0, 0), QSizeF(size)), kAdd);
                set4(a.u.shape, size.width(), size.height(), 0, 0);
                set4(a.u.color0, tint[0] * amount, tint[1] * amount, tint[2] * amount, 1);
                a.tex = current;
                a.aux = soft->texture.get();
                a.blend = false;
                passes.push_back({sum, Qt::transparent, {a}});
                current = sum->texture.get();
            };
            if (l.glow > 0) glowPass(l.glow, l.glowThreshold, editor::glowRadiusPixels(l.glowRadius, H), editor::kGlowTint);
            if (l.halation > 0) {
                glowPass(l.halation, l.halationThreshold, editor::halationRadiusPixels(l.halationRadius, H), editor::kHalationTint);
            }
            if (mask) {
                Target* soft = blur(passes, current, size, std::clamp(l.backgroundBlur, 0.0, 1.0) * 0.045 * size.height());
                Target* mixed = acquire(size);
                Draw m = draw(proj, QRectF(QPointF(0, 0), QSizeF(size)), kMaskMix);
                set4(m.u.shape, size.width(), size.height(), 0, 0);
                setGrade(m);  // the mask follows the source mapping
                m.tex = current;
                m.aux = soft->texture.get();
                m.mask = mask;
                m.curves = nullptr;
                m.lut = nullptr;
                m.blend = false;
                passes.push_back({mixed, Qt::transparent, {m}});
                current = mixed->texture.get();
            }
            if (l.blur > 0) current = blur(passes, current, size, l.blur * 0.03 * H)->texture.get();
            set4(layer.u.uvMap, 0, 0, 1.0 / size.width(), 1.0 / size.height());
            layer.tex = current;
        } else {
            setGrade(layer);
        }

        if (l.shadow > 0) {
            const double sigma = 0.76 * H * 0.025;
            const double dy = std::lround(destPixels.y() + H * 0.012) - destPixels.y();
            QMatrix4x4 m = canvasProj;
            m.translate(static_cast<float>(destPixels.x()), static_cast<float>(destPixels.y() + dy));
            const double pad = 3 * sigma + 2;
            Draw s = draw(m, QRectF(-pad, -pad, size.width() + 2 * pad, size.height() + 2 * pad), kShadow);
            set4(s.u.shape, size.width(), size.height(), l.radius * H, l.circle ? 1 : 0);
            set4(s.u.grade, 1, 0, 0, sigma);
            set4(s.u.color1, 0, 0, 0, std::clamp(l.shadow, 0.0, 1.0) * 0.75 * l.opacity);
            set4(s.u.look, 1, 0, 0, kShadow);
            main.push_back(s);
        }

        const double border = l.border > 0 ? std::max(1.0, l.border * H) : 0.0;
        const double pad = 1.0 + border / 2;
        std::memcpy(layer.u.mvp, model.constData(), sizeof(layer.u.mvp));
        set4(layer.u.quad, -pad, -pad, size.width() + 2 * pad, size.height() + 2 * pad);
        set4(layer.u.shape, size.width(), size.height(), l.radius * H, l.circle ? 1 : 0);
        set4(layer.u.look, l.opacity, border, std::clamp(l.vignette, 0.0, 1.0), kMedia);
        if (l.grain > 0) set4(layer.u.fx, l.grain, editor::grainSizePixels(l.grainSize, H), l.grainSeed & 0xFFFFFFu, 0);
        if (prepare) {  // already converted, graded and mirrored
            set4(layer.u.grade, 1, 0, 0, 0);
            set4(layer.u.grade2, 0, 0, 0, 0);
            set4(layer.u.inputColor, 0, 0, 0, 0);
            set4(layer.u.nodeInfo, 0, 0, 0, -1);
            layer.mask = nullptr;
            layer.lut = nullptr;
            set4(layer.u.lutScale, 0, 0, 0, 0);
        }
        const QColor bc = parseColor(l.borderColor, Qt::white);
        set4(layer.u.color0, bc.redF(), bc.greenF(), bc.blueF(), bc.alphaF());
        main.push_back(layer);
    }

    /// The passes that draw `plan` into the canvas (last pass).
    std::vector<Pass> buildFrame(const RenderPlan& plan, QSize size, const ImageSource& images) {
        const double W = size.width();
        const double H = size.height();
        if (!canvas_ || canvas_->size != size) canvas_ = makeTarget(size);
        for (auto& t : pool_) t->busy = false;
        if (!pending_) pending_ = rhi_->nextResourceUpdateBatch();

        const QMatrix4x4 canvasProj = projection(size);
        std::vector<Pass> passes;
        std::vector<Draw> main;
        const QColor bg = parseColor(plan.background, QColor(14, 15, 19));
        if (!plan.background2.empty()) {
            Draw g = draw(canvasProj, QRectF(0, 0, W, H), kGradient);
            set4(g.u.shape, W, H, 0, 0);
            const QColor end = parseColor(plan.background2, QColor(14, 15, 19));
            set4(g.u.color0, bg.redF(), bg.greenF(), bg.blueF(), 1);
            set4(g.u.color1, end.redF(), end.greenF(), end.blueF(), 1);
            g.blend = false;
            main.push_back(g);
        }
        std::vector<QRectF> titles;
        std::size_t mediaSlot = 0;
        std::size_t overlaySlot = 0;
        for (std::size_t i = 0; i < plan.layers.size();) {
            const VisualLayer& l = plan.layers[i];
            if (l.kind == LayerKind::Media) {
                addMedia(passes, main, canvasProj, l, W, H, images, mediaSlot++);
                ++i;
                continue;
            }
            std::vector<const VisualLayer*> run;  // consecutive text and subtitles: one overlay
            while (i < plan.layers.size() && plan.layers[i].kind != LayerKind::Media) run.push_back(&plan.layers[i++]);
            QRhiTexture* overlay = overlayTexture(overlaySlot++, run, plan, titles, size);
            Draw d = draw(canvasProj, QRectF(0, 0, W, H), kOverlay);
            set4(d.u.uvMap, 0, 0, 1.0 / W, 1.0 / H);
            d.tex = overlay;
            main.push_back(d);
        }
        passes.push_back({canvas_.get(), bg, std::move(main)});
        return passes;
    }

    /// Records and runs `passes`; `readbacks` are read after the last pass.
    bool execute(std::vector<Pass>& passes, const std::vector<std::pair<QRhiTexture*, QRhiReadbackResult*>>& readbacks) {

        // Uniforms for every draw, then the passes.
        std::size_t drawCount = 0;
        for (const Pass& p : passes) drawCount += p.draws.size();
        if (drawCount > uboCapacity_) {
            uboCapacity_ = drawCount * 2;
            ubuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                        static_cast<quint32>(uboStride() * static_cast<int>(uboCapacity_))));
            ubuf_->create();
        }
        QRhiCommandBuffer* cb = nullptr;
        if (rhi_->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return false;
        srbs_.clear();
        int slot = 0;
        for (Pass& p : passes) {
            for (Draw& d : p.draws) {
                pending_->updateDynamicBuffer(ubuf_.get(), static_cast<quint32>(slot * uboStride()), sizeof(Uniforms), &d.u);
                std::unique_ptr<QRhiShaderResourceBindings> srb(rhi_->newShaderResourceBindings());
                const auto b = bindings(slot, d.tex, d.curves, d.lut, d.aux, d.mask);
                srb->setBindings(b.cbegin(), b.cend());
                srb->create();
                srbs_.push_back(std::move(srb));
                ++slot;
            }
        }
        slot = 0;
        const QRhiCommandBuffer::VertexInput vertex(vbuf_.get(), 0);
        for (std::size_t pi = 0; pi < passes.size(); ++pi) {
            Pass& p = passes[pi];
            QRhiResourceUpdateBatch* after = nullptr;
            if (pi + 1 == passes.size()) {
                after = rhi_->nextResourceUpdateBatch();
                for (const auto& [texture, result] : readbacks) after->readBackTexture(QRhiReadbackDescription(texture), result);
            }
            cb->beginPass(p.target->target.get(), p.clear, {1.0f, 0}, pi == 0 ? pending_ : nullptr);
            if (pi == 0) pending_ = nullptr;
            cb->setViewport({0, 0, static_cast<float>(p.target->size.width()), static_cast<float>(p.target->size.height())});
            for (const Draw& d : p.draws) {
                cb->setGraphicsPipeline(d.pipeline ? d.pipeline : d.blend ? blendPipeline_.get() : copyPipeline_.get());
                cb->setShaderResources(srbs_[static_cast<std::size_t>(slot++)].get());
                cb->setVertexInput(0, 1, &vertex);
                cb->draw(4);
            }
            cb->endPass(after);
        }
        return rhi_->endOffscreenFrame() == QRhi::FrameOpSuccess;
    }

    bool renderGpu(const RenderPlan& plan, QImage& target, const ImageSource& images) {
        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        const QSize size = target.size();
        std::vector<Pass> passes = buildFrame(plan, size, images);
        const auto t1 = Clock::now();
        QRhiReadbackResult readback;
        if (!execute(passes, {{canvas_->texture.get(), &readback}})) return false;
        const auto t3 = Clock::now();
        if (readback.data.size() < static_cast<qsizetype>(size.width()) * size.height() * 4) return false;

        // BGRA8 bytes are RGB32's layout; RGBA8 needs a swizzle.
        const bool flip = rhi_->isYUpInFramebuffer();
        const auto* bytes = reinterpret_cast<const uchar*>(readback.data.constData());
        const int stride = size.width() * 4;
        if (format_ == QRhiTexture::BGRA8 && target.format() == QImage::Format_RGB32) {
            for (int y = 0; y < size.height(); ++y) {
                std::memcpy(target.scanLine(y), bytes + static_cast<std::ptrdiff_t>(flip ? size.height() - 1 - y : y) * stride,
                            static_cast<std::size_t>(stride));
            }
        } else {
            const QImage view(bytes, size.width(), size.height(), stride,
                              format_ == QRhiTexture::BGRA8 ? QImage::Format_ARGB32_Premultiplied
                                                            : QImage::Format_RGBA8888_Premultiplied);
            target = (flip ? view.flipped(Qt::Vertical) : view).convertToFormat(target.format());
        }
        if (profile_) {
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            std::fprintf(stderr, "render frame: prepare %.2f ms, gpu+readback %.2f ms, copy %.2f ms\n", ms(t0, t1),
                         ms(t1, t3), ms(t3, Clock::now()));
        }
        return true;
    }

    std::unique_ptr<editor::FrameRenderer> cpu_ = editor::makeCpuRenderer();
    std::filesystem::path projectDir_;
    editor::LutCache luts_;
    std::unique_ptr<QRhi> rhi_;
    bool failed_ = false;
    const bool profile_ = qEnvironmentVariableIsSet("LECTERN_RENDER_PROFILE");
    QRhiTexture::Format format_ = QRhiTexture::BGRA8;
    QRhiResourceUpdateBatch* pending_ = nullptr;
    std::unique_ptr<QRhiBuffer> vbuf_;
    std::unique_ptr<QRhiBuffer> ubuf_;
    std::size_t uboCapacity_ = 0;
    std::unique_ptr<QRhiSampler> linear_;
    std::unique_ptr<QRhiSampler> nearest_;
    std::unique_ptr<QRhiTexture> dummy2D_;
    std::unique_ptr<QRhiTexture> dummy3D_;
    std::unique_ptr<Target> templateTarget_;
    std::unique_ptr<QRhiShaderResourceBindings> templateSrb_;
    std::unique_ptr<QRhiGraphicsPipeline> blendPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> copyPipeline_;
    std::unique_ptr<Target> canvas_;
    std::unique_ptr<Target> lumaTarget_;
    std::unique_ptr<Target> chromaTarget_;
    std::unique_ptr<QRhiGraphicsPipeline> lumaPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> chromaPipeline_;
    QShader vs_;
    QShader fs_;
    std::vector<std::unique_ptr<Target>> pool_;
    std::vector<std::unique_ptr<QRhiShaderResourceBindings>> srbs_;
    std::vector<SourceTexture> sources_;
    std::map<std::string, LutTexture> luts3D_;
    std::vector<Overlay> overlays_;
    std::vector<MaskEntry> masks_;

public:
    ~GpuRenderer() override {
        // GPU objects go before the device that made them.
        if (pending_) pending_->release();
        srbs_.clear();
        masks_.clear();
        overlays_.clear();
        luts3D_.clear();
        sources_.clear();
        pool_.clear();
        canvas_.reset();
        lumaPipeline_.reset();
        chromaPipeline_.reset();
        lumaTarget_.reset();
        chromaTarget_.reset();
        blendPipeline_.reset();
        copyPipeline_.reset();
        templateSrb_.reset();
        templateTarget_.reset();
        dummy3D_.reset();
        dummy2D_.reset();
        nearest_.reset();
        linear_.reset();
        ubuf_.reset();
        vbuf_.reset();
        rhi_.reset();
    }
};

}  // namespace

std::unique_ptr<editor::FrameRenderer> makeGpuRenderer() {
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS) || defined(Q_OS_WIN)
    return std::make_unique<GpuRenderer>();
#else
    return nullptr;
#endif
}

std::string installGpuRenderer() {
    GpuRenderer probe;
    if (!probe.init()) {
        LEC_INFO("render", "no GPU backend; the CPU renderer is used");
        return {};
    }
    std::string backend = probe.name();
    editor::setRendererFactory([] { return makeGpuRenderer(); });
    return backend;
}

}  // namespace lectern::render
