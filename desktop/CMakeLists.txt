# OpenStint desktop: a wxWidgets GUI around the RTL-SDR decoder.
#
# Built from its own directory so the CLI decoders in src/ never depend on
# wxWidgets. It compiles the decoder core from src/ itself, because the core is
# configured at compile time (SAMPLES_PER_SYMBOL), and links the same vendored
# rtl-sdr as openstint_rtlsdr (cmake/Dependencies.cmake).

find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
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

target_compile_features(openstint_desktop PRIVATE cxx_std_20)
# RTL-SDR maxes out at 2.5 MSPS, same as openstint_rtlsdr
target_compile_definitions(openstint_desktop PRIVATE SAMPLES_PER_SYMBOL=2)
target_compile_options(openstint_desktop PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
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

install(TARGETS openstint_desktop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
