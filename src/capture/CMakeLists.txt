add_library(lectern_capture STATIC
    CaptureTypes.cpp
    SessionClock.cpp
    LiveSources.cpp
    MuxWorker.cpp
    RecordingStats.cpp
    VideoTrackWriter.cpp
    AudioTrackWriter.cpp
    SessionManifest.cpp
    ActiveSessionRegistry.cpp
    DiskSpaceMonitor.cpp
    EncodingPresets.cpp
    RecordingSession.cpp
    RecoveryService.cpp
    synthetic/SyntheticSources.cpp
)
add_library(Lectern::capture ALIAS lectern_capture)

target_link_libraries(lectern_capture PUBLIC lectern_core lectern_media lectern_audio)
lectern_configure_target(lectern_capture)
