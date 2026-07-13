cmake_minimum_required(VERSION 3.20)

project(PacketPath VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

option(PACKET_PATH_FETCH_RAYLIB "Download raylib when no package is installed" OFF)

find_package(raylib 5.5 QUIET)
if(NOT TARGET raylib AND NOT TARGET raylib::raylib)
    if(NOT PACKET_PATH_FETCH_RAYLIB)
        message(FATAL_ERROR
            "raylib 5.5+ was not found. Install raylib or configure with "
            "-DPACKET_PATH_FETCH_RAYLIB=ON")
    endif()
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
endif()

if(TARGET raylib::raylib)
    set(PACKET_PATH_RAYLIB_TARGET raylib::raylib)
else()
    set(PACKET_PATH_RAYLIB_TARGET raylib)
endif()

set(PACKET_PATH_SOURCES
    src/AclEngine.cpp
    src/BgpEngine.cpp
    src/Cable.cpp
    src/ConfigPanel.cpp
    src/Device.cpp
    src/EvpnEngine.cpp
    src/Font.cpp
    src/GameUI.cpp
    src/LdpEngine.cpp
    src/Level.cpp
    src/LevelCatalog.cpp
    src/NetworkCanvas.cpp
    src/OspfEngine.cpp
    src/Packet.cpp
    src/ProtocolOverlays.cpp
    src/ProtocolPanels.cpp
    src/ProtocolPanelController.cpp
    src/RsvpEngine.cpp
    src/RsvpReplay.cpp
    src/SceneSerializer.cpp
    src/SimulationEngine.cpp
    src/SoundEngine.cpp
    src/SrEngine.cpp
    src/Srv6Engine.cpp
    src/TraceModal.cpp
    src/UI.cpp)

add_library(packet_path_lib STATIC ${PACKET_PATH_SOURCES})
target_compile_features(packet_path_lib PUBLIC cxx_std_17)
target_include_directories(packet_path_lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(packet_path_lib PUBLIC ${PACKET_PATH_RAYLIB_TARGET})
if(MSVC)
    target_compile_options(packet_path_lib PRIVATE /W4 /permissive-)
else()
    target_compile_options(packet_path_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(packet-path src/main.cpp)
target_link_libraries(packet-path PRIVATE packet_path_lib)

foreach(resource_directory assets levels)
    add_custom_command(TARGET packet-path POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/${resource_directory}
                $<TARGET_FILE_DIR:packet-path>/${resource_directory})
endforeach()

if(BUILD_TESTING)
    add_executable(packet-path-tests tests/simulator_tests.cpp)
    target_link_libraries(packet-path-tests PRIVATE packet_path_lib)
    add_test(NAME simulator COMMAND packet-path-tests)
    set_tests_properties(simulator PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

install(TARGETS packet-path RUNTIME DESTINATION . BUNDLE DESTINATION .)
install(DIRECTORY assets levels DESTINATION .)
install(FILES LICENSE README.md DESTINATION .)

set(CPACK_PACKAGE_NAME "PacketPath")
set(CPACK_PACKAGE_VENDOR "PiMP Studios")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_FILE_NAME
    "PacketPath-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP")
include(CPack)
