#!/bin/sh
# Runs as the .deb's postrm. $1 is "upgrade", "remove", "purge", ...
set -e

if [ "$1" = "upgrade" ]; then
    exit 0
fi

if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
fi

# Only a purge discards state. A plain 'remove' keeps the RC4 registry so
# that reinstalling does not lose the transponder key material.
#
# No user or group to delete: DynamicUser=yes means none was ever created.
if [ "$1" = "purge" ]; then
    rm -rf /var/lib/openstint
    # Only present if an earlier build used StateDirectory= under
    # DynamicUser=, which put the real directory here behind a symlink.
    rm -rf /var/lib/private/openstint

    # plugdev is left alone: it is shared with librtlsdr0/libhackrf0 and with
    # any human users that were added to it.
fi

exit 0
