# GPU renderer (docs/v2/ARCHITECTURE_V2.md §3, ROADMAP_PHASES 1.1): draws the
# same RenderPlan as the CPU compositor, on Qt RHI (Metal / Direct3D 11).
# Registered at startup with editor::setRendererFactory.
add_library(lectern_render STATIC
    GpuRenderer.h
    GpuRenderer.cpp
)
add_library(Lectern::render ALIAS lectern_render)

qt_add_shaders(lectern_render "lectern_render_shaders"
    PREFIX "/lectern/render"
    GLSL "330"
    HLSL 50
    MSL 12
    FILES
        shaders/quad.vert
        shaders/layer.frag
)

target_link_libraries(lectern_render PUBLIC lectern_editor Qt6::Gui PRIVATE Qt6::GuiPrivate)
target_compile_definitions(lectern_render PUBLIC LECTERN_HAS_GPU_RENDERER=1)
lectern_configure_target(lectern_render)
