#!/bin/sh
# Runs as the .deb's prerm. $1 is "upgrade" when an upgrade is replacing us.
set -e

# On upgrade the service must keep running; postinstall restarts it into the
# new binary. Stopping here would mean a gap in lap capture for no reason.
if [ "$1" = "upgrade" ]; then
    exit 0
fi

if command -v systemctl >/dev/null 2>&1; then
    for unit in openstint.service bridge-zround.service; do
        systemctl stop "$unit" || true
        systemctl disable "$unit" || true
    done
fi

exit 0
