# Use C++20 (or newer if needed)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-return-type-c-linkage -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

option(USE_HACKRF "Enable HackRF support" ON)
option(USE_RTLSDR "Enable RTL-SDR support" ON)

set(SAMPLES_PER_SYMBOL 4 CACHE STRING "HackRF samples per symbol (2, 4, or 8)")

# Base source files
set(OPENSTINT_BASE_SOURCES
    frame.cpp
    transponder.cpp
    passing.cpp
    counters.cpp
)

# Required dependencies
find_path(LIQUID_INCLUDE_DIR NAMES liquid/liquid.h)
find_library(LIQUID_LIB REQUIRED NAMES liquid liquid-dsp)

find_path(FEC_INCLUDE_DIR NAMES fec.h)
find_library(FEC_LIB REQUIRED NAMES fec)

find_package(cppzmq REQUIRED)

if(USE_HACKRF)
    find_path(HACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h)
    find_library(HACKRF_LIB REQUIRED NAMES hackrf)

    add_executable(openstint_hackrf main.cpp ${OPENSTINT_BASE_SOURCES})
    target_compile_definitions(openstint_hackrf PRIVATE SAMPLES_PER_SYMBOL=${SAMPLES_PER_SYMBOL})
    target_compile_options(openstint_hackrf PRIVATE -I ".")
    target_include_directories(openstint_hackrf PRIVATE ${LIQUID_INCLUDE_DIR} ${HACKRF_INCLUDE_DIR} ${cppzmq_INCLUDE_DIR} ${FEC_INCLUDE_DIR})
    target_link_libraries(openstint_hackrf
      ${LIQUID_LIB}
      ${HACKRF_LIB}
      ${FEC_LIB}
      cppzmq
      m
    )
endif()

if(USE_RTLSDR)
    find_path(RTLSDR_INCLUDE_DIR NAMES rtl-sdr.h)
    find_library(RTLSDR_LIB REQUIRED NAMES rtlsdr)
    find_path(HACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h)
    find_library(HACKRF_LIB REQUIRED NAMES hackrf)

    add_executable(openstint_rtlsdr main.cpp ${OPENSTINT_BASE_SOURCES})
    target_compile_definitions(openstint_rtlsdr PRIVATE SAMPLES_PER_SYMBOL=2)
    target_compile_options(openstint_rtlsdr PRIVATE -I ".")
    target_include_directories(openstint_rtlsdr PRIVATE ${LIQUID_INCLUDE_DIR} ${HACKRF_INCLUDE_DIR} ${cppzmq_INCLUDE_DIR} ${FEC_INCLUDE_DIR})
    target_link_libraries(openstint_rtlsdr
      ${LIQUID_LIB}
      ${HACKRF_LIB}
      ${FEC_LIB}
      cppzmq
      m
    )
endif()
