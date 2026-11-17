cmake_minimum_required(VERSION 3.28)
project(OpenStint LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(Dependencies)

add_subdirectory(src)

# wxWidgets GUI for RTL-SDR (desktop/); off by default so the CLI build needs no wxWidgets
option(BUILD_DESKTOP "Build the wxWidgets desktop app (RTL-SDR)" OFF)
if(BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()
