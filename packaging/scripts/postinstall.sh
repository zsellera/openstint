#!/bin/sh
# Runs as the .deb's postinst. $1 is "configure" on install and upgrade.
set -e

STATE_DIR=/var/lib/openstint
SVC_USER=openstint

# --- service account -------------------------------------------------------
# A .deb must not assume a 'pi' account exists, so the service runs as a
# dedicated system user created here.
if ! getent group plugdev >/dev/null 2>&1; then
    addgroup --system plugdev
fi

if ! getent passwd "$SVC_USER" >/dev/null 2>&1; then
    adduser --system --no-create-home --shell /usr/sbin/nologin \
            --gecos "OpenStint laptiming decoder" "$SVC_USER"
fi

# plugdev is where librtlsdr0 / libhackrf0 udev rules put the SDR device
# nodes; without it the service cannot open the radio.
if getent group plugdev >/dev/null 2>&1; then
    adduser "$SVC_USER" plugdev >/dev/null 2>&1 || true
fi

# systemd creates StateDirectory= itself, but creating it here means the
# ownership is right even if the unit is never started through systemd.
mkdir -p "$STATE_DIR"
chown "$SVC_USER":"$SVC_USER" "$STATE_DIR"
chmod 0750 "$STATE_DIR"

# --- units -----------------------------------------------------------------
# Guarded so the package stays installable in a container without systemd,
# which is what the CI verification job installs into.
if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true

    # Enable on first install only ($2 is the previous version on upgrade).
    # Re-enabling on every upgrade would silently undo an admin's decision to
    # disable the service. bridge-zround.service is deliberately never
    # enabled -- which bridge you want depends on your timing software.
    if [ -z "${2:-}" ]; then
        systemctl enable openstint.service || true
    fi

    if ! systemctl restart openstint.service; then
        echo "openstint: service failed to start -- is the SDR plugged in?" >&2
        echo "openstint: check 'journalctl -u openstint.service -n 50'" >&2
        # Do not fail the install: a Pi configured before the radio arrives
        # should still end up with a correctly installed package.
    fi
fi

exit 0
