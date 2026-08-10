#!/bin/sh
# Runs as the .deb's postrm. $1 is "upgrade", "remove", "purge", ...
set -e

STATE_DIR=/var/lib/openstint
SVC_USER=openstint

if [ "$1" = "upgrade" ]; then
    exit 0
fi

if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
fi

# Only a purge discards state. A plain 'remove' keeps the RC4 registry so
# that reinstalling does not lose the transponder key material.
if [ "$1" = "purge" ]; then
    rm -rf "$STATE_DIR"

    if getent passwd "$SVC_USER" >/dev/null 2>&1; then
        deluser --system "$SVC_USER" >/dev/null 2>&1 || true
    fi
    # plugdev is left alone: it is shared with librtlsdr0/libhackrf0 and with
    # any human users that were added to it.
fi

exit 0
