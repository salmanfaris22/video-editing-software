#include "editor/FrameRenderer.h"

#include "editor/Compositor.h"

#include <QByteArray>

#include <mutex>

namespace lectern::editor {

namespace {

class CpuRenderer final : public FrameRenderer {
public:
    void setProjectDirectory(std::filesystem::path dir) override { compositor_.setProjectDirectory(std::move(dir)); }
    void render(const RenderPlan& plan, QImage& target, const ImageSource& images) override {
        compositor_.render(plan, target, images);
    }
    [[nodiscard]] std::string name() const override { return "cpu"; }

private:
    Compositor compositor_;
};

std::mutex& factoryMutex() {
    static std::mutex m;
    return m;
}

RendererFactory& factorySlot() {
    static RendererFactory f;
    return f;
}

}  // namespace

void setRendererFactory(RendererFactory factory) {
    std::lock_guard lock(factoryMutex());
    factorySlot() = std::move(factory);
}

std::unique_ptr<FrameRenderer> makeCpuRenderer() { return std::make_unique<CpuRenderer>(); }

std::unique_ptr<FrameRenderer> makeRenderer() {
    if (qgetenv("LECTERN_RENDERER") == "cpu") return makeCpuRenderer();
    RendererFactory factory;
    {
        std::lock_guard lock(factoryMutex());
        factory = factorySlot();
    }
    if (factory) {
        if (auto renderer = factory()) return renderer;
    }
    return makeCpuRenderer();
}

}  // namespace lectern::editor
