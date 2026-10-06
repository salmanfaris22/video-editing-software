add_library(lectern_core STATIC
    Rational.cpp
    Time.cpp
    Error.cpp
    Log.cpp
    Uuid.cpp
    Clock.cpp
    Thread.cpp
    FileSystem.cpp
    Settings.cpp
    Json.cpp
)
add_library(Lectern::core ALIAS lectern_core)

target_include_directories(lectern_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(lectern_core PUBLIC Threads::Threads nlohmann_json::nlohmann_json)
target_compile_definitions(lectern_core PUBLIC
    LECTERN_VERSION="${PROJECT_VERSION}"
    LECTERN_PRODUCT_NAME="${LECTERN_PRODUCT_NAME}")
lectern_configure_target(lectern_core)
