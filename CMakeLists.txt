cmake_minimum_required(VERSION 3.28)
project(OpenStint LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(Dependencies)

add_subdirectory(src)
