#!/usr/bin/env bash
#
# Build openstint_<version>_<arch>.deb with nfpm.
#
# Expects the binaries to already be built:
#   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -B build .
#   ninja -C build
#   packaging/build-deb.sh
#
# Environment:
#   VERSION       package version   (default: 0.$(date -u +%y%m%d).${BUILD_NUMBER:-0})
#   BUILD_NUMBER  used by the default VERSION only
#   BUILD_DIR     cmake build dir   (default: build)
#   OUT_DIR       .deb output dir   (default: dist)
#
# Requires: nfpm, dpkg-shlibdeps (dpkg-dev), dpkg-deb.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Same expression the CI workflow uses, so a local build produces a
# comparable version string. dpkg compares version chunks alternately as text
# and as integers, so 0.260810.0 < 0.260810.1 < 0.260811.0 < 0.270101.0.
VERSION="${VERSION:-0.$(date -u +%y%m%d).${BUILD_NUMBER:-0}}"
ARCH="${ARCH:-$(dpkg --print-architecture)}"
BUILD_DIR="${BUILD_DIR:-build}"
OUT_DIR="${OUT_DIR:-dist}"

BIN_RTLSDR="${BUILD_DIR}/src/openstint_rtlsdr"
BIN_HACKRF="${BUILD_DIR}/src/openstint_hackrf"

for bin in "$BIN_RTLSDR" "$BIN_HACKRF"; do
    if [ ! -x "$bin" ]; then
        echo "error: $bin not found or not executable." >&2
        echo "       build first: cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -B ${BUILD_DIR} . && ninja -C ${BUILD_DIR}" >&2
        exit 1
    fi
done

for tool in nfpm dpkg-shlibdeps dpkg-deb; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool not on PATH" >&2; exit 1; }
done

SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT

# --- runtime dependencies --------------------------------------------------
# The runtime package names are not reliably guessable from the -dev names, so
# ask dpkg-shlibdeps rather than hand-maintaining the list. It insists on a
# debian/control existing, hence the stub; -O prints to stdout instead of
# writing a substvars file.
mkdir -p "$SCRATCH/debian"
cat > "$SCRATCH/debian/control" <<EOF
Source: openstint
Section: misc
Priority: optional
Maintainer: Attila Zseller <zsellera@gmail.com>

Package: openstint
Architecture: ${ARCH}
Description: dpkg-shlibdeps stub, not shipped
EOF

echo "==> computing dependencies with dpkg-shlibdeps"
shlibdeps_raw="$(cd "$SCRATCH" && dpkg-shlibdeps -O \
    "${REPO_ROOT}/${BIN_RTLSDR}" \
    "${REPO_ROOT}/${BIN_HACKRF}")"

shlibs_depends="$(printf '%s\n' "$shlibdeps_raw" | sed -n 's/^shlibs:Depends=//p')"
if [ -z "$shlibs_depends" ]; then
    echo "error: dpkg-shlibdeps produced no shlibs:Depends line" >&2
    echo "       raw output: ${shlibdeps_raw}" >&2
    exit 1
fi

DEPS_FILE="$SCRATCH/depends.yaml"
: > "$DEPS_FILE"
printf '%s\n' "$shlibs_depends" \
    | tr ',' '\n' \
    | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
    | grep -v '^$' \
    | while IFS= read -r dep; do
          printf '  - "%s"\n' "$dep" >> "$DEPS_FILE"
      done

# shlibdeps only sees ELF binaries, so the Python bridges' dependencies have
# to be added by hand.
printf '  - "%s"\n' "python3" "python3-zmq" >> "$DEPS_FILE"

echo "--- computed depends ---"
cat "$DEPS_FILE"
echo "------------------------"

# --- generate nfpm.yaml ----------------------------------------------------
mkdir -p "$BUILD_DIR" "$OUT_DIR"
NFPM_YAML="${BUILD_DIR}/nfpm.yaml"

awk -v version="$VERSION" -v arch="$ARCH" -v builddir="$BUILD_DIR" -v depsfile="$DEPS_FILE" '
    # Exact match, not a substring: a comment merely mentioning the marker
    # must not be replaced by the dependency list.
    $0 == "@DEPENDS@" {
        while ((getline line < depsfile) > 0) print line
        close(depsfile)
        next
    }
    {
        gsub(/@VERSION@/, version)
        gsub(/@ARCH@/, arch)
        gsub(/@BUILDDIR@/, builddir)
        print
    }
' packaging/nfpm.yaml.in > "$NFPM_YAML"

echo "==> packaging openstint ${VERSION} (${ARCH})"
nfpm package -f "$NFPM_YAML" -p deb -t "$OUT_DIR"

DEB="${OUT_DIR}/openstint_${VERSION}_${ARCH}.deb"
if [ ! -f "$DEB" ]; then
    echo "error: expected ${DEB} to exist after packaging" >&2
    ls -l "$OUT_DIR" >&2
    exit 1
fi

# --- sanity check the payload ---------------------------------------------
# Mainly to catch the integrations/*.py glob silently expanding to nothing,
# which would produce a package that installs but has no bridges.
echo "==> verifying package contents"
contents="$(dpkg-deb -c "$DEB")"

for path in \
    ./usr/bin/openstint_rtlsdr \
    ./usr/bin/openstint_hackrf \
    ./lib/systemd/system/openstint.service \
    ./lib/systemd/system/bridge-zround.service \
    ./etc/openstint.conf \
    ./usr/share/doc/openstint/copyright \
    ./usr/share/openstint/integrations/bridge-zround.py
do
    if ! printf '%s\n' "$contents" | grep -q "[[:space:]]${path}\$"; then
        echo "error: ${path} missing from ${DEB}" >&2
        printf '%s\n' "$contents" >&2
        exit 1
    fi
done

py_count="$(printf '%s\n' "$contents" | grep -c '/usr/share/openstint/integrations/.*\.py$' || true)"
if [ "$py_count" -lt 2 ]; then
    echo "error: only ${py_count} bridge script(s) packaged -- glob likely did not expand" >&2
    exit 1
fi

echo
dpkg-deb -I "$DEB"
echo "==> built ${DEB}"
