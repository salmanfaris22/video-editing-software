add_library(lectern_audio STATIC
    LevelMeter.cpp
    TimestampSmoother.cpp
    DriftController.cpp
    SpliceDelayLine.cpp
    NullAudioOutput.cpp
)
add_library(Lectern::audio ALIAS lectern_audio)

target_link_libraries(lectern_audio PUBLIC lectern_core)
lectern_configure_target(lectern_audio)
