# Third-party dependencies. Every dependency is found as an installed package
# first (Homebrew on macOS, vcpkg/system packages elsewhere). Small
# header-only libraries fall back to FetchContent so a fresh checkout builds.

include(FetchContent)

find_package(Threads REQUIRED)

find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swscale swresample avfilter)
message(STATUS "FFmpeg libavcodec ${FFmpeg_avcodec_VERSION}")

find_package(nlohmann_json 3.11 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not installed - fetching")
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(LECTERN_BUILD_TESTS)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GoogleTest not installed - fetching")
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

if(LECTERN_BUILD_APP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg)
    qt_standard_project_setup(REQUIRES 6.8)
    message(STATUS "Qt ${Qt6_VERSION}")
elseif(LECTERN_BUILD_TESTS OR LECTERN_BUILD_TOOLS)
    # Without the app, QtGui still enables the editor engine (compositor,
    # export) with its tests and CLI — e.g. in sanitizer builds.
    find_package(Qt6 6.8 QUIET COMPONENTS Core Gui Qml)
    if(Qt6_FOUND)
        qt_standard_project_setup(REQUIRES 6.8)
        message(STATUS "Qt ${Qt6_VERSION} (editor engine only)")
    else()
        message(STATUS "Qt not found: building without the editor engine")
    endif()
endif()

# GPU renderer (src/render): Qt RHI + shaders baked at build time by qsb.
# Metal (macOS) and Direct3D (Windows); elsewhere the CPU compositor is used.
set(LECTERN_GPU_RENDERER OFF)
if(TARGET Qt6::Gui AND (APPLE OR WIN32))
    # QRhi ties us to this Qt build (docs/v2/ARCHITECTURE_V2.md §7: pin the Qt version).
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 QUIET COMPONENTS ShaderTools GuiPrivate)
    if(TARGET Qt6::GuiPrivate AND COMMAND qt_add_shaders)
        set(LECTERN_GPU_RENDERER ON)
    endif()
endif()
message(STATUS "GPU renderer: ${LECTERN_GPU_RENDERER}")
