# ---------------------------------------------------------------------------
# Vendored third-party libraries.
#
# rtl-sdr and libhackrf are built from source and linked statically into the
# decoders on every platform, pinned to the revisions below. The alternative --
# whatever each package manager happens to ship -- produced materially
# different builds of the same decoder, with librtlsdr ranging from 0.6.0 to
# 2.0.2 depending on the distribution release.
#
# For rtl-sdr this is not tidiness, it is the only way to control which driver
# runs. Hardware support lives entirely inside the library: v2.0.3 added the
# RTL-SDR Blog V4L in tuner_r82xx.c with no change to rtl-sdr.h at all, and
# the soname has been librtlsdr.so.0 continuously since 0.5.3. A binary built
# against new headers therefore loads a years-old .so without complaint and
# simply does not see the dongle -- no link error, no runtime error, and no
# rtlsdr_get_version() in the public API to assert on.
#
# liquid-dsp is built from source too, for a different reason: Windows has no
# package for it, and the prebuilt MinGW archive CI used to download is gone.
# Building it everywhere keeps one liquid version on every platform.
#
# libfec, libusb-1.0 and ZeroMQ stay system libraries on purpose. None of them
# carries hardware knowledge, so a version difference costs accuracy at worst,
# never "does not see the dongle", and libusb in particular is the
# kernel-facing USB backend and should match the host. Debian and Ubuntu
# package all three; Homebrew packages all but libfec, which macOS builders
# install from source (see README.md).
#
# The .deb still declares runtime dependencies on rtl-sdr and hackrf even
# though it no longer links them -- see packaging/build-deb.sh for why.
# ---------------------------------------------------------------------------

include(FetchContent)

# --- pins ------------------------------------------------------------------
# Bump deliberately: these decide which hardware the shipped binaries support.
set(OPENSTINT_RTLSDR_TAG "v2.0.3")      # 2026-08-11, adds RTL-SDR Blog V4L
set(OPENSTINT_HACKRF_TAG "v2026.01.3")  # 2026-01-30
set(OPENSTINT_LIQUID_TAG "v1.8.3")      # must match cmake/liquid-clangcl/

# FetchContent brings its dependencies in as subdirectories, and IMPORTED
# targets are only visible in the directory that created them and below.
# libhackrf's find_package(LIBUSB) target has to reach src/, where the
# executables are linked, so promote find_package() imports to global scope.
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET GLOBAL libusb-1.0)

# ---------------------------------------------------------------------------
# rtl-sdr
# ---------------------------------------------------------------------------
# EXCLUDE_FROM_ALL keeps upstream's eight rtl_* utilities out of the build and,
# more to the point, keeps their install() rules from landing in our .deb.
# INSTALL_UDEV_RULES is forced off for the same reason -- upstream installs to
# an absolute /etc/udev/rules.d, which a package must never write to directly.
set(INSTALL_UDEV_RULES OFF CACHE BOOL "" FORCE)

# Match how Debian builds librtlsdr, so vendoring does not silently change
# device behaviour for existing Pi users. Without DETACH_KERNEL_DRIVER an
# rtlsdr_open() fails outright when the dvb_usb_rtl28xxu kernel module has
# already claimed the dongle, which is the default state on a fresh Pi.
# Zero-copy is left off on ARM exactly as debian/rules does.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DETACH_KERNEL_DRIVER ON CACHE BOOL "" FORCE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm)")
        set(ENABLE_ZEROCOPY OFF CACHE BOOL "" FORCE)
    else()
        set(ENABLE_ZEROCOPY ON CACHE BOOL "" FORCE)
    endif()
endif()

FetchContent_Declare(rtlsdr
    GIT_REPOSITORY https://github.com/osmocom/rtl-sdr.git
    GIT_TAG        ${OPENSTINT_RTLSDR_TAG}
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(rtlsdr)

# Upstream's src/CMakeLists.txt links libusb to the shared target twice and
# never to the static one, so rtlsdr_static carries no link dependencies at
# all. Supply them here rather than patching the checkout.
target_link_libraries(rtlsdr_static PUBLIC PkgConfig::LIBUSB Threads::Threads)

# rtl-sdr_export.h resolves RTLSDR_API to __declspec(dllimport) unless the
# consumer defines this. MinGW takes the __GNUC__ branch and does not care,
# but MSVC would link against import stubs that do not exist.
target_compile_definitions(rtlsdr_static PUBLIC rtlsdr_STATIC)

add_library(openstint::rtlsdr ALIAS rtlsdr_static)

# ---------------------------------------------------------------------------
# libhackrf
# ---------------------------------------------------------------------------
# Populated but not added to the build -- see the libfec block below for how
# and why that works. libhackrf is a single C file, and building it directly
# is considerably less trouble than upstream's CMakeLists.txt, which
#
#   - defines an `uninstall` target, a fatal name clash with rtl-sdr's. It is
#     guarded on HackRF_SOURCE_DIR, which is only set when host/CMakeLists.txt
#     runs -- and that also builds hackrf-tools, which we do not want.
#   - hardcodes the shared target's name inside the routine shared by both
#     library variants, so configuring with ENABLE_SHARED_LIB=OFF fails.
#   - calls export(EXPORT HackRFTargets), which errors out under
#     EXCLUDE_FROM_ALL because the install() rules that populate that export
#     set are skipped.
#
# Both version macros have #ifndef fallbacks upstream; they are set here so
# hackrf_library_version() still reports the truth. HACKRF_BIG_ENDIAN is
# deliberately absent -- every platform OpenStint targets is little-endian.
FetchContent_Declare(hackrf
    GIT_REPOSITORY https://github.com/greatscottgadgets/hackrf.git
    GIT_TAG        ${OPENSTINT_HACKRF_TAG}
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  openstint-builds-this-itself
)
FetchContent_MakeAvailable(hackrf)

set(OPENSTINT_HACKRF_SRC "${hackrf_SOURCE_DIR}/host/libhackrf/src")

add_library(openstint_hackrf_lib STATIC "${OPENSTINT_HACKRF_SRC}/hackrf.c")
target_compile_definitions(openstint_hackrf_lib PRIVATE
    LIBRARY_VERSION="0.9.2"
    LIBRARY_RELEASE="${OPENSTINT_HACKRF_TAG}"
)
target_link_libraries(openstint_hackrf_lib PUBLIC PkgConfig::LIBUSB Threads::Threads)
set_target_properties(openstint_hackrf_lib PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)

# In the source tree the header sits at src/hackrf.h; the <libhackrf/hackrf.h>
# spelling main_hackrf.cpp uses only exists once installed. Mirror the
# installed layout so the source keeps one include form on every platform.
set(OPENSTINT_HACKRF_SHIM "${CMAKE_BINARY_DIR}/vendor-include")
file(COPY "${OPENSTINT_HACKRF_SRC}/hackrf.h"
     DESTINATION "${OPENSTINT_HACKRF_SHIM}/libhackrf")
target_include_directories(openstint_hackrf_lib SYSTEM
    PUBLIC  "${OPENSTINT_HACKRF_SHIM}"
    PRIVATE "${OPENSTINT_HACKRF_SRC}")

add_library(openstint::hackrf ALIAS openstint_hackrf_lib)

# ---------------------------------------------------------------------------
# liquid-dsp
# ---------------------------------------------------------------------------
# Static library only, without upstream's examples, tests and benchmarks.
# SIMD detection stays on: v1.8.3 compiles each SSE/AVX/NEON variant with a
# target attribute and picks one at runtime by cpuid, so a binary built on a
# newer CPU still runs on an older one.
#
# cmake/liquid-clangcl/ is copied verbatim from CubicSDR
# (github.com/cjcliffe/CubicSDR, cmake/liquid-clangcl/ at commit
# e2b2188909a9add7253a501e56a2df3823f64ef1), which maintains it for its own
# Windows build of this same liquid tag. Do not edit those files; when bumping
# OPENSTINT_LIQUID_TAG, take CubicSDR's set for the new tag. Every patch is
# guarded by _WIN32 or _MSC_VER, so they are applied on all platforms and
# change nothing outside Windows. Three take effect under MinGW (strsep,
# unistd.h, timers); the other two and the complex.h shim are clang-cl only.
find_package(Git REQUIRED)
set(OPENSTINT_LIQUID_SUPPORT "${CMAKE_CURRENT_LIST_DIR}/liquid-clangcl")
file(GLOB OPENSTINT_LIQUID_PATCHES CONFIGURE_DEPENDS "${OPENSTINT_LIQUID_SUPPORT}/patches/*.patch")
list(SORT OPENSTINT_LIQUID_PATCHES)

# checkout first, so re-running the patch step on an existing checkout does
# not fail on patches that are already applied.
FetchContent_Declare(liquid
    GIT_REPOSITORY https://github.com/jgaeddert/liquid-dsp.git
    GIT_TAG        ${OPENSTINT_LIQUID_TAG}
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  "${GIT_EXECUTABLE}" checkout -- .
          COMMAND  "${GIT_EXECUTABLE}" apply --whitespace=nowarn ${OPENSTINT_LIQUID_PATCHES}
    EXCLUDE_FROM_ALL
    SYSTEM
)

# block() keeps these switches, BUILD_SHARED_LIBS in particular, from leaking
# into the rest of the project. CMP0077 lets them override liquid's option()
# defaults, which its cmake_minimum_required(3.10) would otherwise ignore.
block()
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    # liquid prints ~150 lines of SIMD probing and a configuration summary;
    # keep its warnings and errors, drop the rest.
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    set(BUILD_SHARED_LIBS  OFF)
    set(BUILD_STATIC_LIBS  ON)
    set(BUILD_EXAMPLES     OFF)
    set(BUILD_AUTOTESTS    OFF)
    set(BUILD_BENCHMARKS   OFF)
    set(BUILD_SANDBOX      OFF)
    set(BUILD_DOC          OFF)
    set(FIND_FFTW          OFF) # liquid's own FFT; no optional system dependency
    set(ENABLE_TIMESTAMPS  OFF) # no build date/host baked into the binaries
    if(MSVC)
        # CubicSDR's clang-cl flags. Untested here: CI builds with MinGW.
        string(APPEND CMAKE_C_FLAGS
            " -I\"${OPENSTINT_LIQUID_SUPPORT}/shim\" -D_USE_MATH_DEFINES -D_CRT_SECURE_NO_WARNINGS")
    endif()
    FetchContent_MakeAvailable(liquid)
endblock()

# Upstream links the library to `c` and `m`. Neither matters for a static
# archive until it reaches our link line, where MinGW has no libc.a and fails
# on -lc. Keep libm off Windows too, where the C runtime already provides it.
set_property(TARGET liquid-static PROPERTY
    INTERFACE_LINK_LIBRARIES "$<$<NOT:$<PLATFORM_ID:Windows>>:m>")

add_library(openstint::liquid ALIAS liquid-static)

# ---------------------------------------------------------------------------
# libfec
# ---------------------------------------------------------------------------
# Not vendored. Only the K=9 r=1/2 Viterbi decoder is used (transponder.cpp),
# an API that has not changed in twenty years, so the packaged copy is as good
# as a pinned one. Debian and Ubuntu ship it as libfec-dev; Homebrew has no
# formula, so macOS builders install quiet/libfec by hand and Windows CI
# compiles it -- see README.md and .github/workflows/windows-build.yml.
find_path(FEC_INCLUDE_DIR NAMES fec.h)
find_library(FEC_LIB REQUIRED NAMES fec)

# Exposed as openstint::fec so src/CMakeLists.txt does not care where it came
# from -- static on Windows, the distribution's shared object elsewhere.
add_library(openstint_fec INTERFACE)
target_include_directories(openstint_fec SYSTEM INTERFACE ${FEC_INCLUDE_DIR})
target_link_libraries(openstint_fec INTERFACE ${FEC_LIB})
add_library(openstint::fec ALIAS openstint_fec)
