# mobile/ — desktop side of phone cameras (Phase 1C)
#
# A connected phone becomes an ordinary capture::IVideoSource whose host
# timestamps come from the arrival clock, so the recording engine needs no
# special cases. See docs/PHONE_CAMERA_PROTOCOL.md.
add_library(lectern_mobile STATIC
    PhoneCameraSource.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/PhonePage.cpp"
)
add_library(Lectern::mobile ALIAS lectern_mobile)

target_link_libraries(lectern_mobile
    PUBLIC
        lectern_capture
        lectern_media
    PRIVATE
        lectern_network)
lectern_configure_target(lectern_mobile)

# The phone page is real HTML on disk so it stays editable and lintable, then
# inlined into the binary so hosting it needs no installed assets.
file(GLOB LECTERN_PHONE_ASSETS CONFIGURE_DEPENDS "*.html")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/phone_page.html" LECTERN_PHONE_PAGE)
configure_file(PhonePage.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/PhonePage.cpp" @ONLY)