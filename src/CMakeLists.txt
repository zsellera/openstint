cmake_minimum_required(VERSION 3.27)
project(openstint_decoder CXX)

# Use C++20 (or newer if needed)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the main executable
add_executable(main main.cpp frame.cpp passing.cpp)

find_path(LIQUID_INCLUDE_DIR NAMES liquid/liquid.h)
find_library(LIQUID_LIB REQUIRED NAMES liquid liquid-dsp)

find_path(HACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h)
find_library(HACKRF_LIB REQUIRED NAMES hackrf)

# find_path(ZeroMQ_INCLUDE_DIR NAMES zmq.hpp)
# find_library(ZeroMQ_LIBRARY NAMES zmq)

find_package(cppzmq REQUIRED)

target_compile_options(main PRIVATE -O0 -march=armv8-a -Wno-return-type-c-linkage -Wno-deprecated-declarations -I ".")
target_include_directories(main PRIVATE ${LIQUID_INCLUDE_DIR} ${HACKRF_INCLUDE_DIR} ${cppzmq_INCLUDE_DIR})
target_link_libraries(main
    ${LIQUID_LIB}
    ${HACKRF_LIB}
    ${ZeroMQ_LIBRARY}
    cppzmq
    m
    c
)
