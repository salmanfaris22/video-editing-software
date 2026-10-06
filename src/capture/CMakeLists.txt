add_library(lectern_capture STATIC
    CaptureTypes.cpp
    SessionClock.cpp
    LiveSources.cpp
    MuxWorker.cpp
    RecordingStats.cpp
    VideoTrackWriter.cpp
    AudioTrackWriter.cpp
    SessionManifest.cpp
    InputEventRecorder.cpp
    ActiveSessionRegistry.cpp
    DiskSpaceMonitor.cpp
    EncodingPresets.cpp
    RecordingSession.cpp
    RecoveryService.cpp
    synthetic/SyntheticSources.cpp
)
add_library(Lectern::capture ALIAS lectern_capture)

target_link_libraries(lectern_capture PUBLIC lectern_core lectern_media lectern_audio)
if(APPLE)
    target_sources(lectern_capture PRIVATE macos/InputEventRecorderMac.mm)
    target_link_libraries(lectern_capture PRIVATE "-framework CoreGraphics" "-framework CoreFoundation")
    target_compile_definitions(lectern_capture PRIVATE LECTERN_HAS_NATIVE_INPUT_CAPTURE=1)
    set_source_files_properties(macos/InputEventRecorderMac.mm PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()
lectern_configure_target(lectern_capture)
