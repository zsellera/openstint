#!/bin/sh
# Runs as the .deb's postinst. $1 is "configure", $2 the previous version on
# an upgrade (empty on first install).
set -e

# No account management here. The units use DynamicUser=yes, so systemd
# allocates a transient UID/GID at start and re-chowns StateDirectory= to it.
# That removes the whole class of problems this script used to have: no
# assumption of a 'pi' account, no adduser (absent from minimal Debian since
# trixie), no user to clean up on purge, no leaked UID.

# The one thing that must exist beforehand: SupplementaryGroups= in the unit
# refers to a group by name, and the service fails to start if it is missing.
# plugdev normally comes from base-passwd; create it only as insurance.
if ! getent group plugdev >/dev/null 2>&1 && command -v groupadd >/dev/null 2>&1; then
    groupadd --system plugdev
fi

# The RC4 registry ships in the .deb as root:users 0775. Re-assert ownership
# and add the setgid bit here: Go's os.FileMode does not represent setgid the
# way the tar header does, so it is not reliable through the packaging path,
# and without it files created by the service get the transient DynamicUser
# group instead of 'users' -- leaving them unreadable to real logins.
STATE_DIR=/var/lib/openstint
if [ -d "$STATE_DIR" ] && getent group users >/dev/null 2>&1; then
    chown root:users "$STATE_DIR"
    chmod 2775 "$STATE_DIR"
fi

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

    # restart, not start: on an upgrade this is what picks up the new binary.
    if ! systemctl restart openstint.service; then
        echo "openstint: service failed to start -- is the SDR plugged in?" >&2
        echo "openstint: check 'journalctl -u openstint.service -n 50'" >&2
        # Do not fail the install: a Pi configured before the radio arrives
        # should still end up with a correctly installed package.
    fi
fi

exit 0
