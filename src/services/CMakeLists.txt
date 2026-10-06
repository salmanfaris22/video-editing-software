add_library(lectern_services STATIC
    RecordingImporter.cpp
    RecordingSetup.cpp
)
add_library(Lectern::services ALIAS lectern_services)

target_link_libraries(lectern_services PUBLIC lectern_capture lectern_project lectern_media lectern_mobile)
lectern_configure_target(lectern_services)
