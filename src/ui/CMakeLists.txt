# Desktop application: Qt Quick UI over the Qt-free engine. The QML module
# (view-models + QML) is a static library so that tests can load the real
# QML and drive it with mouse and keyboard events; the app links it.

qt_add_library(lectern_ui STATIC)
find_package(Qt6 REQUIRED COMPONENTS Network)
# App icon: assets/brand/lectern-app-icon.svg rendered to .icns (macOS) and .ico (Windows).
set(LECTERN_APP_ICON_MAC "${PROJECT_SOURCE_DIR}/src/app/icons/Lectern.icns")
set_source_files_properties("${LECTERN_APP_ICON_MAC}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
qt_add_executable(lectern MACOSX_BUNDLE WIN32 ../app/main.cpp "${LECTERN_APP_ICON_MAC}")

set_target_properties(lectern PROPERTIES
    OUTPUT_NAME "${LECTERN_PRODUCT_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/src/app/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${LECTERN_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "${LECTERN_PRODUCT_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "Lectern.icns")

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

file(GLOB LECTERN_ICONS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS icons/*.svg)

qt_add_qml_module(lectern_ui
    URI Lectern.UI
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/AssistantsDialog.qml
        qml/Theme.qml
        qml/components/IconButton.qml
        qml/components/MiniSlider.qml
        qml/components/ScrubField.qml
        qml/components/PrimaryButton.qml
        qml/components/Card.qml
        qml/components/FieldLabel.qml
        qml/components/SourcePicker.qml
        qml/components/ShareLinkPicker.qml
        qml/components/Segmented.qml
        qml/components/ToggleRow.qml
        qml/components/LevelMeter.qml
        qml/components/StatusChip.qml
        qml/components/Banner.qml
        qml/components/Icon.qml
        qml/screens/HomeScreen.qml
        qml/screens/RecordScreen.qml
        qml/screens/CountdownOverlay.qml
        qml/screens/RecordingHud.qml
        qml/screens/EditorScreen.qml
        qml/components/SliderRow.qml
        qml/components/ColorSwatches.qml
        qml/components/SectionLabel.qml
        qml/components/EmptyHint.qml
        qml/components/InputField.qml
        qml/editor/EditorTopBar.qml
        qml/editor/ToolRail.qml
        qml/editor/ToolPanel.qml
        qml/editor/PreviewPane.qml
        qml/editor/CanvasOverlay.qml
        qml/editor/InlineTextEditor.qml
        qml/editor/TimelinePanel.qml
        qml/editor/TrackHeader.qml
        qml/editor/ClipItem.qml
        qml/editor/ColorPage.qml
        qml/editor/color/ColorWheel.qml
        qml/editor/ExportDialog.qml
        qml/editor/panels/SetupPanel.qml
        qml/editor/panels/LayoutPanel.qml
        qml/editor/panels/CutPanel.qml
        qml/editor/panels/EffectsPanel.qml
        qml/editor/panels/OverlayPanel.qml
        qml/editor/panels/StylePanel.qml
        qml/editor/panels/TextAnimationSection.qml
        qml/editor/panels/SubtitlesPanel.qml
        qml/editor/panels/AudioPanel.qml
        qml/editor/panels/AdjustPanel.qml
        qml/editor/panels/KeyframesSection.qml
    SOURCES
        AppController.h AppController.cpp
        RecorderController.h RecorderController.cpp
        ProjectController.h ProjectController.cpp ProjectEdits.cpp ProjectLooks.cpp ProjectColor.cpp ProjectCanvas.cpp
        ProjectClipKeyframes.cpp ProjectTracks.cpp
        ProjectAssistant.cpp
        McpController.h McpController.cpp McpTools.cpp
        PlaybackController.h PlaybackController.cpp
        EditorPreviewItem.h EditorPreviewItem.cpp
        ExportController.h ExportController.cpp
        VideoPreviewItem.h VideoPreviewItem.cpp
        ThumbnailProvider.h ThumbnailProvider.cpp
        WaveformProvider.h WaveformProvider.cpp
        ScopeItem.h ScopeItem.cpp
        IconProvider.h IconProvider.cpp
        SerialExecutor.h
    RESOURCES
        ${LECTERN_ICONS}
        brand/lectern-icon-256.png
)

target_include_directories(lectern_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(lectern_ui PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg
    lectern_services lectern_platform lectern_editor)
target_link_libraries(lectern_ui PUBLIC lectern_mcp Qt6::Network)
lectern_configure_target(lectern_ui)

target_link_libraries(lectern PRIVATE lectern_ui lectern_uiplugin)
if(TARGET lectern_render)
    target_link_libraries(lectern PRIVATE lectern_render)
endif()
lectern_configure_target(lectern)

if(APPLE)
    # macOS ties privacy grants (Screen Recording, Camera, Microphone) to the
    # signing identity. Ad-hoc signatures change with every build, so grants
    # would be lost; tools/macos/setup-dev-signing.sh creates a stable local
    # identity that cmake/sign-macos-app.sh uses when present.
    option(LECTERN_SIGN_APP "Sign the app bundle after building (stable local identity when set up)" ON)
    if(LECTERN_SIGN_APP)
        add_custom_command(TARGET lectern POST_BUILD
            COMMAND "${PROJECT_SOURCE_DIR}/cmake/sign-macos-app.sh" "$<TARGET_BUNDLE_DIR:lectern>"
            COMMENT "Signing ${LECTERN_PRODUCT_NAME}.app"
            VERBATIM)
    endif()
endif()

if(WIN32)
    include(${PROJECT_SOURCE_DIR}/cmake/LecternPackaging.cmake)
endif()
