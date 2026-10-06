add_library(lectern_project STATIC
    Project.cpp
    ProjectJson.cpp
    ProjectStore.cpp
)
add_library(Lectern::project ALIAS lectern_project)

target_link_libraries(lectern_project PUBLIC lectern_core lectern_timeline)
lectern_configure_target(lectern_project)
