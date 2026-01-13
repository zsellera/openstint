# Use C++20 (or newer if needed)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-return-type-c-linkage -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

add_executable(openstint main.cpp frame.cpp transponder.cpp passing.cpp counters.cpp)

find_path(LIQUID_INCLUDE_DIR NAMES liquid/liquid.h)
find_library(LIQUID_LIB REQUIRED NAMES liquid liquid-dsp)

find_path(HACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h)
find_library(HACKRF_LIB REQUIRED NAMES hackrf)

find_path(FEC_INCLUDE_DIR NAMES fec.h)
find_library(FEC_LIB REQUIRED NAMES fec)

find_package(cppzmq REQUIRED)

target_compile_options(openstint PRIVATE -I ".")
target_include_directories(openstint PRIVATE ${LIQUID_INCLUDE_DIR} ${HACKRF_INCLUDE_DIR} ${cppzmq_INCLUDE_DIR} ${FEC_INCLUDE_DIR})
target_link_libraries(openstint
    ${LIQUID_LIB}
    ${HACKRF_LIB}
    ${FEC_LIB}
    cppzmq
    m
)
