include(GoogleTest)

# Shared helpers (temp directories, media decoding for verification).
add_library(lectern_test_support STATIC support/TestSupport.cpp support/TestMedia.cpp)
target_include_directories(lectern_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lectern_test_support PUBLIC lectern_media GTest::gtest)
lectern_configure_target(lectern_test_support)

function(lectern_add_test_executable name)
    cmake_parse_arguments(ARG "" "LABEL" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LIBS} lectern_test_support GTest::gtest_main)
    lectern_configure_target(${name})
    if(NOT ARG_LABEL)
        set(ARG_LABEL unit)
    endif()
    gtest_discover_tests(${name}
        DISCOVERY_TIMEOUT 120
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS ${ARG_LABEL} TIMEOUT 300)
endfunction()

lectern_add_test_executable(lectern_core_tests
    SOURCES unit/core/TimeTest.cpp unit/core/QueueTest.cpp unit/core/FileSystemTest.cpp unit/core/MiscTest.cpp
    LIBS lectern_core)

lectern_add_test_executable(lectern_mcp_tests
    SOURCES unit/mcp/ServerTest.cpp
    LIBS lectern_mcp)

lectern_add_test_executable(lectern_audio_tests
    SOURCES unit/audio/AudioDspTest.cpp
    LIBS lectern_audio)

lectern_add_test_executable(lectern_media_tests
    SOURCES unit/media/EncodeMuxTest.cpp unit/media/ProbeTest.cpp unit/media/VideoFramePoolTest.cpp
            unit/media/ReaderTest.cpp
    LIBS lectern_media)

lectern_add_test_executable(lectern_timeline_tests
    SOURCES unit/timeline/TimelineTest.cpp unit/timeline/EditOpsTest.cpp unit/timeline/SubtitlesTest.cpp
            unit/project/ProjectTest.cpp
    LIBS lectern_project)

lectern_add_test_executable(lectern_capture_tests
    SOURCES unit/capture/SessionClockTest.cpp unit/capture/VideoPacerTest.cpp unit/capture/CaptureMiscTest.cpp
            unit/capture/TimestampMappingTest.cpp
            integration/RecordingSessionTest.cpp integration/RecoveryTest.cpp
    LIBS lectern_capture lectern_services)

lectern_add_test_executable(lectern_long_tests
    LABEL long
    SOURCES integration/LongRecordingTest.cpp
    LIBS lectern_capture lectern_services)

lectern_add_test_executable(lectern_mobile_tests
    SOURCES unit/mobile/PhoneCameraTest.cpp
    LIBS lectern_mobile lectern_network)

# Helper process for the crash-recovery test (records until SIGKILLed).
add_executable(lectern_crash_helper integration/CrashHelper.cpp)
target_link_libraries(lectern_crash_helper PRIVATE lectern_capture)
lectern_configure_target(lectern_crash_helper)
add_dependencies(lectern_capture_tests lectern_crash_helper)
target_compile_definitions(lectern_capture_tests PRIVATE
    LECTERN_CRASH_HELPER="$<TARGET_FILE:lectern_crash_helper>")

# Editing engine (needs QtGui; QGuiApplication on the offscreen platform).
if(TARGET lectern_editor)
    add_executable(lectern_editor_tests
        editor/EditorTestMain.cpp
        editor/EditorFixture.cpp
        editor/RenderPlanTest.cpp
        editor/AudioMixerTest.cpp
        editor/CompositorTest.cpp
        editor/CanvasGeometryTest.cpp
        editor/ColorGradingTest.cpp
        editor/LooksTest.cpp
        editor/NodesTest.cpp
        editor/FilmEffectsTest.cpp
        editor/ExportTest.cpp
        editor/PlaybackTest.cpp)
    target_link_libraries(lectern_editor_tests PRIVATE lectern_editor lectern_test_support GTest::gtest)
    lectern_configure_target(lectern_editor_tests)
    gtest_discover_tests(lectern_editor_tests
        DISCOVERY_TIMEOUT 120
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS unit TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# UI view-models (the C++ QML talks to), compiled from the app sources.
if(TARGET lectern_editor AND TARGET Qt6::Qml)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    set(LECTERN_UI_DIR "${PROJECT_SOURCE_DIR}/src/ui")
    add_executable(lectern_ui_tests
        ui/UiTestMain.cpp
        ui/ControllerTest.cpp
        ui/SegmentationTest.cpp
        ui/McpTest.cpp
        editor/EditorFixture.cpp
        ${LECTERN_UI_DIR}/ProjectController.h ${LECTERN_UI_DIR}/ProjectController.cpp
        ${LECTERN_UI_DIR}/ProjectEdits.cpp ${LECTERN_UI_DIR}/ProjectLooks.cpp
        ${LECTERN_UI_DIR}/ProjectColor.cpp ${LECTERN_UI_DIR}/ProjectCanvas.cpp
        ${LECTERN_UI_DIR}/ProjectClipKeyframes.cpp
        ${LECTERN_UI_DIR}/ProjectTracks.cpp ${LECTERN_UI_DIR}/ProjectGrades.cpp ${LECTERN_UI_DIR}/ProjectNodes.cpp
        ${LECTERN_UI_DIR}/FrameGrab.cpp ${LECTERN_UI_DIR}/LookPreviews.cpp
        ${LECTERN_UI_DIR}/ProjectAssistant.cpp
        ${LECTERN_UI_DIR}/McpController.h ${LECTERN_UI_DIR}/McpController.cpp ${LECTERN_UI_DIR}/McpTools.cpp
        ${LECTERN_UI_DIR}/PlaybackController.h ${LECTERN_UI_DIR}/PlaybackController.cpp
        ${LECTERN_UI_DIR}/ExportController.h ${LECTERN_UI_DIR}/ExportController.cpp
        ${LECTERN_UI_DIR}/SerialExecutor.h)
    set_target_properties(lectern_ui_tests PROPERTIES AUTOMOC ON)
    target_include_directories(lectern_ui_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(lectern_ui_tests PRIVATE
        lectern_editor lectern_services lectern_platform lectern_test_support Qt6::Qml Qt6::Gui GTest::gtest)
    target_link_libraries(lectern_ui_tests PRIVATE lectern_mcp Qt6::Network)
    lectern_configure_target(lectern_ui_tests)
    gtest_discover_tests(lectern_ui_tests
        DISCOVERY_TIMEOUT 120
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS unit TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# GPU renderer vs the CPU compositor (needs a GPU backend; skips otherwise).
if(TARGET lectern_render)
    add_executable(lectern_render_tests
        editor/EditorTestMain.cpp
        editor/EditorFixture.cpp
        render/GpuRendererTest.cpp)
    target_include_directories(lectern_render_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(lectern_render_tests PRIVATE lectern_render lectern_test_support GTest::gtest)
    lectern_configure_target(lectern_render_tests)
    gtest_discover_tests(lectern_render_tests
        DISCOVERY_TIMEOUT 120
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS unit TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# The real QML (Lectern.UI) driven with mouse/keyboard events.
if(TARGET lectern_ui)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(lectern_qml_tests
        qml/QmlTestMain.cpp
        qml/QmlHarness.h
        qml/CanvasInteractionTest.cpp
        qml/TimelineInteractionTest.cpp
        qml/ColorPageTest.cpp
        editor/EditorFixture.cpp)
    target_include_directories(lectern_qml_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(lectern_qml_tests PRIVATE
        lectern_ui lectern_uiplugin lectern_test_support Qt6::Test GTest::gtest)
    lectern_configure_target(lectern_qml_tests)
    gtest_discover_tests(lectern_qml_tests
        DISCOVERY_TIMEOUT 120
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS unit TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(TARGET lectern_platform)
    lectern_add_test_executable(lectern_device_tests
        LABEL device
        SOURCES integration/DeviceSmokeTest.cpp
        LIBS lectern_platform lectern_services)
endif()
