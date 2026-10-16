cmake_minimum_required(VERSION 3.22)
project(neebles_connect_desktop LANGUAGES CXX)

# F3: PRODUCTIVE SOURCE recipe; imported libraries exclusively from a verified
# CUSTOM .deb candidate overlay. No system pkg-config/host fallback.
if(NOT DEFINED NEEBLES_CONNECT_CONTROLLED_SYSROOT OR
   NOT CMAKE_SYSROOT STREQUAL NEEBLES_CONNECT_CONTROLLED_SYSROOT)
    message(FATAL_ERROR "NEEBLES Connect requires Esbirro controlled sysroot")
endif()
if(NOT DEFINED NEEBLES_CONNECT_DEP_OVERLAY OR
   NOT IS_DIRECTORY "${NEEBLES_CONNECT_DEP_OVERLAY}/usr/include")
    message(FATAL_ERROR "NEEBLES Connect requires verified CUSTOM development overlay")
endif()
get_filename_component(NEEBLES_CONNECT_DEP_OVERLAY "${NEEBLES_CONNECT_DEP_OVERLAY}" REALPATH)
set(_INCLUDE "${NEEBLES_CONNECT_DEP_OVERLAY}/usr/include")
set(_LIB "${NEEBLES_CONNECT_DEP_OVERLAY}/usr/lib/x86_64-linux-gnu")
foreach(_NEEDED
    "${_INCLUDE}/rfb/rfbclient.h"
    "${_INCLUDE}/sasl/sasl.h"
    "${_INCLUDE}/libssh2.h"
    "${_INCLUDE}/vterm.h"
    "${_LIB}/libvncclient.so.1"
    "${_LIB}/libssh2.so.1"
    "${_LIB}/libvterm.so.0")
    if(NOT EXISTS "${_NEEDED}")
        message(FATAL_ERROR "Missing controlled development/runtime file: ${_NEEDED}")
    endif()
endforeach()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SKIP_BUILD_RPATH ON)
set(CMAKE_SKIP_INSTALL_RPATH ON)
find_package(Qt6 6.8.2 EXACT REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_library(EsbirroVNCClient SHARED IMPORTED GLOBAL)
set_target_properties(EsbirroVNCClient PROPERTIES
    IMPORTED_LOCATION "${_LIB}/libvncclient.so.1"
    INTERFACE_INCLUDE_DIRECTORIES "${_INCLUDE}")
add_library(EsbirroSSH2 SHARED IMPORTED GLOBAL)
set_target_properties(EsbirroSSH2 PROPERTIES
    IMPORTED_LOCATION "${_LIB}/libssh2.so.1"
    INTERFACE_INCLUDE_DIRECTORIES "${_INCLUDE}")
add_library(EsbirroVTerm SHARED IMPORTED GLOBAL)
set_target_properties(EsbirroVTerm PROPERTIES
    IMPORTED_LOCATION "${_LIB}/libvterm.so.0"
    INTERFACE_INCLUDE_DIRECTORIES "${_INCLUDE}")

add_executable(neebles-connect-desktop
    main.cpp MainWindow.cpp MainWindow.h
    VncPane.cpp VncPane.h
    SshPane.cpp SshPane.h
    SshTerminal.cpp SshTerminal.h)
target_link_libraries(neebles-connect-desktop PRIVATE
    Qt6::Widgets Threads::Threads EsbirroVNCClient EsbirroSSH2 EsbirroVTerm)
target_compile_options(neebles-connect-desktop PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(neebles-connect-desktop PRIVATE
    -Wl,--no-allow-shlib-undefined
    "-Wl,-rpath-link,${_LIB}"
    "-Wl,-rpath-link,${NEEBLES_CONNECT_CONTROLLED_SYSROOT}/usr/lib/x86_64-linux-gnu"
    "-Wl,-rpath-link,${NEEBLES_CONNECT_CONTROLLED_SYSROOT}/lib/x86_64-linux-gnu")

# Relocatable layout: <module-root>/bin/ + manifest.json + assets/.
get_filename_component(NEEBLES_CONNECT_MODULE_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
install(TARGETS neebles-connect-desktop RUNTIME DESTINATION bin)
install(FILES "${NEEBLES_CONNECT_MODULE_SOURCE_ROOT}/manifest.json" DESTINATION ".")
install(DIRECTORY "${NEEBLES_CONNECT_MODULE_SOURCE_ROOT}/assets/" DESTINATION assets
        FILES_MATCHING PATTERN "*.png")
