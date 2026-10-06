cmake_minimum_required(VERSION 3.25)

set(LECTERN_PRODUCT_NAME "Lectern" CACHE STRING "User-visible product name")
set(LECTERN_BUNDLE_ID "com.lectern.desktop" CACHE STRING "macOS bundle identifier / app id")

project(Lectern
    VERSION 0.1.0
    DESCRIPTION "Native screen recorder and non-linear editor"
    LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

option(LECTERN_BUILD_APP "Build the Qt Quick desktop application" ON)
option(LECTERN_BUILD_TOOLS "Build command-line tools (lectern-rec, lectern-probe)" ON)
option(LECTERN_BUILD_TESTS "Build unit and integration tests" ON)
option(LECTERN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(LECTERN_ENABLE_ASAN "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LECTERN_ENABLE_TSAN "Build with ThreadSanitizer" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# macOS: the minimum supported OS is 14.0 (ScreenCaptureKit audio and
# preservesAspectRatio, AVFoundation external/continuity camera types).
# Distribution builds pass -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 together with
# dependencies built for that target. Development builds against Homebrew
# (whose libraries target the running macOS) leave it unset, which avoids
# "built for newer macOS" linker warnings.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(LecternCompilerOptions)
include(LecternDependencies)

add_subdirectory(src)

if(LECTERN_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(LECTERN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "${LECTERN_PRODUCT_NAME} ${PROJECT_VERSION}: app=${LECTERN_BUILD_APP} tools=${LECTERN_BUILD_TOOLS} tests=${LECTERN_BUILD_TESTS}")
