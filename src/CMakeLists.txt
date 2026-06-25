# Use C++20 (or newer if needed)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  set(CMAKE_CXX_FLAGS "/W3 /EHsc")
  set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi")
  set(CMAKE_CXX_FLAGS_RELEASE "/O2")
else()
  set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-return-type-c-linkage -Wno-deprecated-declarations")
  set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g -fno-omit-frame-pointer -fno-inline")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

option(USE_HACKRF "Enable HackRF support" ON)
option(USE_RTLSDR "Enable RTL-SDR support" ON)
option(USE_PYTHON "Enable Python bindings (pybind11)" OFF)

set(SAMPLES_PER_SYMBOL 4 CACHE STRING "HackRF samples per symbol (2, 4, or 8)")

# Core sources (decoder engine, no ZMQ, no device drivers)
set(OPENSTINT_CORE_SOURCES
    frame.cpp
    transponder.cpp
    passing.cpp
    counters.cpp
    rc4.cpp
    engine.cpp
)

# App sources (ZMQ reporter, capture replay, crash handler)
set(OPENSTINT_APP_SOURCES
    ${OPENSTINT_CORE_SOURCES}
    commons.cpp
    zmq_reporter.cpp
    capture.cpp
    crash_handler.cpp
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

    add_executable(openstint_hackrf main_hackrf.cpp ${OPENSTINT_APP_SOURCES})
    target_compile_definitions(openstint_hackrf PRIVATE SAMPLES_PER_SYMBOL=${SAMPLES_PER_SYMBOL})
    target_include_directories(openstint_hackrf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LIQUID_INCLUDE_DIR} ${HACKRF_INCLUDE_DIR} ${cppzmq_INCLUDE_DIR} ${FEC_INCLUDE_DIR})
    target_link_libraries(openstint_hackrf
      ${LIQUID_LIB}
      ${HACKRF_LIB}
      ${FEC_LIB}
      cppzmq
    )

    if(WIN32)
      target_link_libraries(openstint_hackrf dbghelp)
    endif()
    if(NOT MSVC)
      target_link_libraries(openstint_hackrf m)
    endif()
endif()

if(USE_RTLSDR)
    find_path(RTLSDR_INCLUDE_DIR NAMES rtl-sdr.h)
    find_library(RTLSDR_LIB REQUIRED NAMES rtlsdr)

    add_executable(openstint_rtlsdr main_rtlsdr.cpp ${OPENSTINT_APP_SOURCES})
    target_compile_definitions(openstint_rtlsdr PRIVATE SAMPLES_PER_SYMBOL=2)
    target_include_directories(openstint_rtlsdr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LIQUID_INCLUDE_DIR} ${RTLSDR_INCLUDE_DIR} ${cppzmq_INCLUDE_DIR} ${FEC_INCLUDE_DIR})
    target_link_libraries(openstint_rtlsdr
      ${LIQUID_LIB}
      ${RTLSDR_LIB}
      ${FEC_LIB}
      cppzmq
    )

    if(WIN32)
      target_link_libraries(openstint_rtlsdr dbghelp)
    endif()
    if(NOT MSVC)
      target_link_libraries(openstint_rtlsdr m)
    endif()
endif()

if(USE_PYTHON)
    find_package(pybind11 REQUIRED)

    pybind11_add_module(openstint python/bindings.cpp ${OPENSTINT_CORE_SOURCES})
    target_compile_definitions(openstint PRIVATE SAMPLES_PER_SYMBOL=2)
    target_include_directories(openstint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LIQUID_INCLUDE_DIR} ${FEC_INCLUDE_DIR})
    target_link_libraries(openstint PRIVATE ${LIQUID_LIB} ${FEC_LIB})
endif()
