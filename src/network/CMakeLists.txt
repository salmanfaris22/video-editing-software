# network/ — LAN transport for phone cameras (Phase 1C).
#
# Only the HTTP surface described in docs/PHONE_CAMERA_PROTOCOL.md §4/§6 that a
# browser can serve is implemented here: a token-guarded page host plus binary
# frame upload. SRTP, mDNS and the TLS control channel land in later phases.
find_package(OpenSSL REQUIRED)

add_library(lectern_network STATIC
    HttpServer.cpp
    TlsServerContext.cpp
)
add_library(Lectern::network ALIAS lectern_network)

target_link_libraries(lectern_network PUBLIC lectern_core OpenSSL::SSL OpenSSL::Crypto)
lectern_configure_target(lectern_network)

if(WIN32)
    target_link_libraries(lectern_network PUBLIC ws2_32 iphlpapi)
endif()