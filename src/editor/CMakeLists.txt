# Editing engine (docs/RENDERING_PIPELINE.md): render plans, the QPainter
# compositor shared by preview and export, the audio mixer, silence
# detection and the exporter. Needs QtGui (images, fonts), not QtQuick.
add_library(lectern_editor STATIC
    LayoutPresets.cpp
    RenderPlan.cpp
    LayerGeometry.cpp
    ColorGrading.cpp
    AudioMixer.cpp
    SilenceDetector.cpp
    FrameProvider.cpp
    Compositor.cpp
    FrameRenderer.cpp
    Looks.cpp
    Exporter.cpp
    PlaybackEngine.cpp
)
add_library(Lectern::editor ALIAS lectern_editor)

target_link_libraries(lectern_editor PUBLIC lectern_project lectern_media lectern_audio Qt6::Gui)
lectern_configure_target(lectern_editor)
