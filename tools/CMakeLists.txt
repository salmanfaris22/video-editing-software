add_executable(lectern-rec lectern-rec/main.cpp)
target_link_libraries(lectern-rec PRIVATE lectern_platform lectern_services)
lectern_configure_target(lectern-rec)

add_executable(lectern-probe lectern-probe/main.cpp)
target_link_libraries(lectern-probe PRIVATE lectern_media)
lectern_configure_target(lectern-probe)

if(TARGET lectern_editor)
    add_executable(lectern-export lectern-export/main.cpp)
    target_link_libraries(lectern-export PRIVATE lectern_editor lectern_platform)
    if(TARGET lectern_render)
        target_link_libraries(lectern-export PRIVATE lectern_render)
    endif()
    lectern_configure_target(lectern-export)
endif()

if(TARGET lectern_editor AND TARGET Qt6::Qml)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    set(LECTERN_CONTROLLER_DIR "${PROJECT_SOURCE_DIR}/src/ui")
    add_executable(lectern-mcp lectern-mcp/main.cpp
        ${LECTERN_CONTROLLER_DIR}/ProjectController.h ${LECTERN_CONTROLLER_DIR}/ProjectController.cpp
        ${LECTERN_CONTROLLER_DIR}/ProjectEdits.cpp ${LECTERN_CONTROLLER_DIR}/ProjectLooks.cpp
        ${LECTERN_CONTROLLER_DIR}/ProjectColor.cpp ${LECTERN_CONTROLLER_DIR}/ProjectCanvas.cpp
        ${LECTERN_CONTROLLER_DIR}/ProjectClipKeyframes.cpp ${LECTERN_CONTROLLER_DIR}/ProjectTracks.cpp
        ${LECTERN_CONTROLLER_DIR}/ProjectAssistant.cpp
        ${LECTERN_CONTROLLER_DIR}/McpController.h ${LECTERN_CONTROLLER_DIR}/McpController.cpp
        ${LECTERN_CONTROLLER_DIR}/McpTools.cpp)
    set_target_properties(lectern-mcp PROPERTIES AUTOMOC ON)
    target_link_libraries(lectern-mcp PRIVATE lectern_mcp lectern_editor Qt6::Qml Qt6::Network)
    lectern_configure_target(lectern-mcp)
endif()
