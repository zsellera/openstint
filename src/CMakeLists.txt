# Use C++20 (or newer if needed)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-return-type-c-linkage -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -g -fno-omit-frame-pointer -fno-inline")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

option(USE_HACKRF "Enable HackRF support" ON)
option(USE_RTLSDR "Enable RTL-SDR support" ON)

set(SAMPLES_PER_SYMBOL 8 CACHE STRING "HackRF samples per symbol (2 or 8)")

# Base source files
set(OPENSTINT_BASE_SOURCES
    frame.cpp
    transponder.cpp
    passing.cpp
    counters.cpp
    commons.cpp
    capture.cpp
    rc4.cpp
    crash_handler.cpp
)

# ZeroMQ is the one dependency still taken from the system: a stable, widely
# packaged ABI that carries no hardware knowledge. Everything else is built
# from a pinned source revision -- see cmake/Dependencies.cmake.
find_package(cppzmq REQUIRED)

# Shared configuration for both decoders. They differ only in their main file,
# their radio library and their symbol rate.
function(openstint_add_decoder target main_source radio_lib samples_per_symbol)
    add_executable(${target} ${main_source} ${OPENSTINT_BASE_SOURCES})
    target_compile_definitions(${target} PRIVATE SAMPLES_PER_SYMBOL=${samples_per_symbol})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE
        ${radio_lib}
        openstint::liquid
        openstint::fec
        cppzmq
        m
    )

    if(WIN32)
      # Symbolising the crash handler's backtrace.
      target_link_libraries(${target} PRIVATE dbghelp)
    endif()

    install(TARGETS ${target}
            RUNTIME_DEPENDENCY_SET openstint_runtime_deps
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

if(USE_HACKRF)
    openstint_add_decoder(openstint_hackrf main_hackrf.cpp openstint::hackrf ${SAMPLES_PER_SYMBOL})
endif()

if(USE_RTLSDR)
    # RTL-SDR maxes out at the required minimum of 2.5 MSPS, so this is fixed.
    openstint_add_decoder(openstint_rtlsdr main_rtlsdr.cpp openstint::rtlsdr 2)
endif()

if(WIN32 AND (USE_HACKRF OR USE_RTLSDR))
    # Replaces the hand-rolled `ldd | grep mingw64 | xargs cp` in the packaging
    # workflow. With the SDR and DSP libraries linked statically the remaining
    # set is just the toolchain runtime, ZeroMQ and libusb, but resolving it
    # here means the release zip cannot drift from what the binaries actually
    # need. Note $<TARGET_RUNTIME_DLLS> would not work: it only sees IMPORTED
    # targets carrying an IMPORTED_LOCATION, which MinGW-found libraries do not.
    #
    # DIRECTORIES is required, not a hint. On Windows the resolver looks in the
    # depending binary's own directory and in system32, and nowhere else -- it
    # never consults PATH, so /mingw64/bin is invisible to it and every MinGW
    # DLL comes back unresolved. The compiler lives in that same directory,
    # which is how we name it without hardcoding the MSYS2 install prefix.
    get_filename_component(OPENSTINT_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(RUNTIME_DEPENDENCY_SET openstint_runtime_deps
            PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
            POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*"
            DIRECTORIES "${OPENSTINT_MINGW_BIN}"
            DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
