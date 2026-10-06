add_library(lectern_timeline STATIC
    Animated.cpp
    Timeline.cpp
    TimelineJson.cpp
    EditOps.cpp
    Subtitles.cpp
)
add_library(Lectern::timeline ALIAS lectern_timeline)

target_link_libraries(lectern_timeline PUBLIC lectern_core)
lectern_configure_target(lectern_timeline)
