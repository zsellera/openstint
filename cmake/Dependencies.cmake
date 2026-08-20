# ---------------------------------------------------------------------------
# Vendored third-party libraries.
#
# rtl-sdr, libhackrf, liquid-dsp and libfec are built from source and linked
# statically into the decoders on every platform, pinned to the revisions
# below. The alternative -- whatever each package manager happens to ship --
# produced four materially different builds of the same decoder: liquid-dsp
# ranged from a prebuilt .dll curl'd out of an unrelated project's repository
# on Windows to 1.7.0 from Homebrew, and librtlsdr from 0.6.0 to 2.0.2
# depending on the distribution release.
#
# For rtl-sdr this is not tidiness, it is the only way to control which driver
# runs. Hardware support lives entirely inside the library: v2.0.3 added the
# RTL-SDR Blog V4L in tuner_r82xx.c with no change to rtl-sdr.h at all, and
# the soname has been librtlsdr.so.0 continuously since 0.5.3. A binary built
# against new headers therefore loads a years-old .so without complaint and
# simply does not see the dongle -- no link error, no runtime error, and no
# rtlsdr_get_version() in the public API to assert on.
#
# libusb-1.0 and ZeroMQ stay system libraries on purpose. libusb is the
# kernel-facing USB backend and should match the host; ZeroMQ is a stable,
# widely packaged ABI that carries no hardware knowledge.
#
# The .deb still declares runtime dependencies on rtl-sdr and hackrf even
# though it no longer links them -- see packaging/build-deb.sh for why.
# ---------------------------------------------------------------------------

include(FetchContent)

# --- pins ------------------------------------------------------------------
# Bump deliberately: these decide which hardware the shipped binaries support.
set(OPENSTINT_RTLSDR_TAG "v2.0.3")      # 2026-08-11, adds RTL-SDR Blog V4L
set(OPENSTINT_HACKRF_TAG "v2026.01.3")  # 2026-01-30
set(OPENSTINT_LIQUID_TAG "v1.7.0")      # 2025-02-01; see the liquid block below
# quiet/libfec publishes no tags; this is the tip of master.
set(OPENSTINT_LIBFEC_TAG "9750ca0a6d0a786b506e44692776b541f90daa91")

# liquid compile flags:
# disable everything since 2008 (intel atom and wonders like that are supported)
set(C_AVX_FOUND      FALSE CACHE BOOL   "" FORCE)
set(C_AVX_FLAGS      ""    CACHE STRING "" FORCE)
set(C_AVX2_FOUND     FALSE CACHE BOOL   "" FORCE)
set(C_AVX2_FLAGS     ""    CACHE STRING "" FORCE)
set(C_AVX512_FOUND   FALSE CACHE BOOL   "" FORCE)
set(C_AVX512_FLAGS   ""    CACHE STRING "" FORCE)
set(CXX_AVX_FOUND    FALSE CACHE BOOL   "" FORCE)
set(CXX_AVX_FLAGS    ""    CACHE STRING "" FORCE)
set(CXX_AVX2_FOUND   FALSE CACHE BOOL   "" FORCE)
set(CXX_AVX2_FLAGS   ""    CACHE STRING "" FORCE)
set(CXX_AVX512_FOUND FALSE CACHE BOOL   "" FORCE)
set(CXX_AVX512_FLAGS ""    CACHE STRING "" FORCE)

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
# Pinned to 1.7.0 rather than the 1.8.x line, deliberately. 1.8 added a core/
# module whose two new files assume a POSIX-ish C11 libc: timer.c wants
# <sys/resource.h> and getrusage(), logging.c wants strsep() and timespec_get().
# MSYS2's MINGW64 environment targets the legacy msvcrt.dll and has none of
# them, and liquid's CI has never built on Windows at all -- windows-latest is
# commented out of its matrix, and its only Windows-aware code is an untested
# `if (MSVC)` plus four "NOTE: MSVC probably invalid" comments in FindSIMD. All
# 160 sources of 1.7.0 compile clean under that toolchain with nothing added.
#
# 1.7.0 has no FFTW detection at all (its CMakeLists says "TODO: check for
# FFTW"), so unlike 1.8 there is no FIND_FFTW to force off and no risk of
# libfftw3 appearing in the .deb's computed depends: on some build hosts.
set(BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BUILD_AUTOTESTS  OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_SANDBOX    OFF CACHE BOOL "" FORCE)
set(BUILD_DOC        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(liquid
    GIT_REPOSITORY https://github.com/jgaeddert/liquid-dsp.git
    GIT_TAG        ${OPENSTINT_LIQUID_TAG}
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(liquid)

# 1.7.0 defines exactly one library target and hardcodes it SHARED -- there is
# no static variant and no BUILD_SHARED_LIBS to flip. Rather than reimplement
# its build, assemble a static archive out of the per-module OBJECT libraries it
# already defines. Those carry the SIMD selection and compile flags chosen by
# its own CMakeLists, so this tracks upstream instead of duplicating it. The
# SHARED target is never built: EXCLUDE_FROM_ALL keeps it out of `all` and
# nothing here depends on it.
set(OPENSTINT_LIQUID_MODULES
    agc audio buffer channel dotprod equalization fec fft filter framing math
    matrix modem multichannel nco optim quantization random sequence utility
    vector)

# The object libraries are compiled without -fPIC upstream, which is fine for
# the archive itself but not for what happens next: Debian's gcc builds
# executables as PIE by default, and linking non-PIC objects into a PIE fails
# outright at link time. Set it before the objects are built.
set(OPENSTINT_LIQUID_OBJECTS "")
foreach(module IN LISTS OPENSTINT_LIQUID_MODULES)
    set_target_properties(${module} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    list(APPEND OPENSTINT_LIQUID_OBJECTS "$<TARGET_OBJECTS:${module}>")
endforeach()

add_library(openstint_liquid STATIC
    "${liquid_SOURCE_DIR}/src/libliquid.c"
    ${OPENSTINT_LIQUID_OBJECTS}
)
set_target_properties(openstint_liquid PROPERTIES POSITION_INDEPENDENT_CODE ON)

# liquid.h is the public header; liquid.internal.h and the generated config.h
# are needed to compile libliquid.c itself and stay private. Upstream reaches
# these through directory-scoped include_directories() calls, which do not
# apply to a target declared out here.
target_include_directories(openstint_liquid SYSTEM
    PUBLIC  "${liquid_SOURCE_DIR}/include"
    PRIVATE "${liquid_SOURCE_DIR}" "${liquid_BINARY_DIR}")

# Upstream links `c m` on its shared target. libm is the part that matters and
# it does not exist as a separate archive on Windows.
if(NOT WIN32)
    target_link_libraries(openstint_liquid PUBLIC m)
endif()

add_library(openstint::liquid ALIAS openstint_liquid)

# ---------------------------------------------------------------------------
# libfec
# ---------------------------------------------------------------------------
# Populated but not added to the build: SOURCE_SUBDIR names a directory that
# does not exist, which tells FetchContent_MakeAvailable to skip
# add_subdirectory(). Three reasons not to use upstream's CMakeLists.txt:
#
#   - its library target is called `fec`, and so is one of liquid-dsp's object
#     libraries. Target names are global, so the two cannot coexist.
#   - it declares cmake_minimum_required(VERSION 3.0), which CMake 4 rejects
#     outright. Homebrew and MSYS2 both ship CMake 4 now.
#   - it uses directory-scoped include_directories(), so fec.h never reaches
#     a consumer, and it builds eight test executables we do not want.
#
# Only the K=9 r=1/2 Viterbi decoder is used (transponder.cpp), so only the
# four files that reach it are compiled. Everything SIMD in libfec is gated on
# __i386__ or __VEC__ and compiles to nothing on x86-64, arm64 or MinGW --
# cpu_mode_unknown.c is what upstream's own build selects on those
# architectures too. The result is the same portable C decoder everywhere.
FetchContent_Declare(libfec
    GIT_REPOSITORY https://github.com/quiet/libfec.git
    GIT_TAG        ${OPENSTINT_LIBFEC_TAG}
    SOURCE_SUBDIR  openstint-builds-this-itself
)
FetchContent_MakeAvailable(libfec)

add_library(openstint_fec STATIC
    "${libfec_SOURCE_DIR}/fec.c"
    "${libfec_SOURCE_DIR}/viterbi29.c"
    "${libfec_SOURCE_DIR}/viterbi29_port.c"
    "${libfec_SOURCE_DIR}/cpu_mode_unknown.c"
)
target_include_directories(openstint_fec SYSTEM PUBLIC "${libfec_SOURCE_DIR}")
set_target_properties(openstint_fec PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)
add_library(openstint::fec ALIAS openstint_fec)
