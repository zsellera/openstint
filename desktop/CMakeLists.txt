# OpenStint desktop: a wxWidgets GUI around the RTL-SDR decoder.
#
# Built from its own directory so the CLI decoders in src/ never depend on
# wxWidgets. It compiles the decoder core from src/ itself, because the core is
# configured at compile time (SAMPLES_PER_SYMBOL), and links the same vendored
# rtl-sdr as openstint_rtlsdr (cmake/Dependencies.cmake).

if(WIN32)
    # MSYS2's wxWidgets package ships static libraries and a CMake package
    # config next to its DLLs. Linking statically keeps wxWidgets and the image
    # libraries it pulls in (png, jpeg, tiff, webp, zlib, ...) out of the
    # release zip, the same way the SDR and DSP libraries are linked.
    set(wxWidgets_USE_STATIC ON)
    find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base)
else()
    find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
endif()
if(NOT TARGET cppzmq)
    find_package(cppzmq REQUIRED)
endif()

set(OPENSTINT_SRC_DIR "${PROJECT_SOURCE_DIR}/src")

# Embed docs/logo.svg as the app icon; re-configure when it changes.
set(OPENSTINT_LOGO "${PROJECT_SOURCE_DIR}/docs/logo.svg")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OPENSTINT_LOGO}")
file(READ "${OPENSTINT_LOGO}" OPENSTINT_LOGO_SVG)
configure_file(logo_svg.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/logo_svg.hpp" @ONLY)

add_executable(openstint_desktop WIN32
    app.cpp
    app_icon.cpp
    charts.cpp
    radio.cpp
    ${OPENSTINT_SRC_DIR}/frame.cpp
    ${OPENSTINT_SRC_DIR}/transponder.cpp
    ${OPENSTINT_SRC_DIR}/passing.cpp
    ${OPENSTINT_SRC_DIR}/counters.cpp
    ${OPENSTINT_SRC_DIR}/commons.cpp
    ${OPENSTINT_SRC_DIR}/rc4.cpp
    ${OPENSTINT_SRC_DIR}/syndrome.cpp
    ${OPENSTINT_SRC_DIR}/crash_handler.cpp
)
if(WIN32)
    target_sources(openstint_desktop PRIVATE openstint_desktop.rc) # manifest
endif()

target_compile_features(openstint_desktop PRIVATE cxx_std_20)
# RTL-SDR maxes out at 2.5 MSPS, same as openstint_rtlsdr
target_compile_definitions(openstint_desktop PRIVATE SAMPLES_PER_SYMBOL=2)
# C++ only: target-wide options also reach windres, which rejects them
target_compile_options(openstint_desktop PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-deprecated-declarations>)
target_include_directories(openstint_desktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${OPENSTINT_SRC_DIR}
)
target_link_libraries(openstint_desktop PRIVATE
    wxWidgets::wxWidgets
    openstint::rtlsdr
    openstint::liquid
    openstint::fec
    cppzmq
)
if(WIN32)
    target_link_libraries(openstint_desktop PRIVATE dbghelp)
endif()

# On Windows the DLLs it still needs (toolchain runtime, ZeroMQ, libusb) are a
# subset of what the CLI decoders need, so they join the same dependency set
# that src/CMakeLists.txt installs.
if(WIN32)
    install(TARGETS openstint_desktop
            RUNTIME_DEPENDENCY_SET openstint_runtime_deps
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
    install(TARGETS openstint_desktop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
