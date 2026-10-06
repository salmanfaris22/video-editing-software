add_library(lectern_packs STATIC PackManager.cpp)
add_library(Lectern::packs ALIAS lectern_packs)
target_link_libraries(lectern_packs PUBLIC lectern_core)
lectern_configure_target(lectern_packs)
