#pragma once

#include <cstdint>
#include <chrono>

class Timebase {
  const uint64_t startup_ts;
  bool mode_sysclk = false;

public:
  Timebase() : startup_ts(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()
  ).count()) {}

  void use_system_clock() { mode_sysclk = true; }

  uint64_t now() {
    if (mode_sysclk) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()
      ).count();
    } else {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()
      ).count() - startup_ts;
    }
  }
};
