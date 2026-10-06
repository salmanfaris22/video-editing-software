# Windows install layout + CPack ZIP (64-bit Windows 10 1903+ and Windows 11).

if(NOT WIN32 OR NOT TARGET lectern)
    return()
endif()

set(_lectern_app_dir "${PROJECT_SOURCE_DIR}/src/app")
set(_lectern_qml_dir "${PROJECT_SOURCE_DIR}/src/ui/qml")
set(_lectern_qml_import "${CMAKE_BINARY_DIR}/qml/Lectern/UI")

target_compile_definitions(lectern PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)

configure_file("${_lectern_app_dir}/lectern-icon.rc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/lectern-icon.rc" @ONLY)
target_sources(lectern PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/lectern-icon.rc")
if(MSVC)
    target_link_options(lectern PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${_lectern_app_dir}/lectern.manifest")
else()
    configure_file("${_lectern_app_dir}/lectern.manifest"
                   "${CMAKE_CURRENT_BINARY_DIR}/lectern.manifest" COPYONLY)
    configure_file("${_lectern_app_dir}/lectern.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/lectern.rc" COPYONLY)
    target_sources(lectern PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/lectern.rc")
endif()

install(TARGETS lectern RUNTIME DESTINATION . COMPONENT app)

find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${Qt6_DIR}/../../../bin" "${Qt6_DIR}/../../bin" "$ENV{QT_ROOT}/bin")

set(_LECTERN_QML_DIR "${_lectern_qml_dir}")
set(_LECTERN_QML_IMPORT "${_lectern_qml_import}")
configure_file("${PROJECT_SOURCE_DIR}/cmake/windeploy-lectern.cmake.in"
               "${CMAKE_BINARY_DIR}/cmake/windeploy-lectern.cmake" @ONLY)

install(SCRIPT "${CMAKE_BINARY_DIR}/cmake/windeploy-lectern.cmake" COMPONENT app)

include(CPack)
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "${LECTERN_PRODUCT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${LECTERN_PRODUCT_NAME}-${PROJECT_VERSION}-win64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
set(CPACK_COMPONENTS_ALL app)

message(STATUS "Windows: cmake --install build/win --prefix dist && cpack -G ZIP")
