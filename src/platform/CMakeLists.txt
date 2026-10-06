add_library(lectern_platform STATIC)
add_library(Lectern::platform ALIAS lectern_platform)

option(LECTERN_NATIVE_CAPTURE "Build native OS capture backends (otherwise synthetic only)" ON)

if(APPLE AND LECTERN_NATIVE_CAPTURE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/macos/MacBackends.mm")
    target_sources(lectern_platform PRIVATE
        macos/MacBackends.mm
        macos/MacPermissions.mm
        macos/MacScreenCapture.mm
        macos/MacCameraCapture.mm
        macos/MacAudioCapture.mm
        macos/MacFrames.mm
        macos/MacGlobalHotkeys.mm
        macos/MacAudioOutput.mm
        macos/MacPersonSegmentation.mm
    )
    target_link_libraries(lectern_platform PRIVATE
        "-framework Vision"
        "-framework Foundation"
        "-framework AppKit"
        "-framework ScreenCaptureKit"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreGraphics"
        "-framework VideoToolbox"
        "-framework IOSurface"
        "-framework Carbon")
    lectern_enable_objc_arc(lectern_platform)
    target_compile_definitions(lectern_platform PUBLIC LECTERN_HAS_NATIVE_CAPTURE=1)
elseif(WIN32 AND LECTERN_NATIVE_CAPTURE)
    # Windows 10 1903+ (Windows.Graphics.Capture), WASAPI, Media Foundation.
    target_sources(lectern_platform PRIVATE
        windows/WinBackends.cpp
        windows/WinAudioCapture.cpp
        windows/WinCameraCapture.cpp
        windows/WinScreenCapture.cpp
        windows/WinD3D11.cpp
        windows/WinGlobalHotkeys.cpp
        windows/WinAudioOutput.cpp
        PersonSegmentationStub.cpp
    )
    target_compile_definitions(lectern_platform PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(lectern_platform PRIVATE
        d3d11 dxgi dwmapi mfplat mfreadwrite mf mfuuid ole32 avrt runtimeobject version shell32 advapi32 user32)
    target_compile_definitions(lectern_platform PUBLIC LECTERN_HAS_NATIVE_CAPTURE=1)
else()
    target_sources(lectern_platform PRIVATE FallbackBackends.cpp PersonSegmentationStub.cpp)
endif()

target_link_libraries(lectern_platform PUBLIC lectern_capture)
lectern_configure_target(lectern_platform)
