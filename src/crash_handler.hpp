#pragma once

// Installs a crash handler that writes a full minidump (.dmp) next to the
// executable when the process crashes. No-op on non-Windows platforms.
void install_crash_handler();
