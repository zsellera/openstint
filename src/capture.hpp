#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Callback signature matching rtlsdr_read_async_cb_t, so the RTL-SDR binary can
// hand its existing rx_callback straight to the replayer. The HackRF binary
// reinterprets the raw bytes appropriately (uint8 vs int8).
// ctx is an rx_clock_t: the callback calls it for the buffer's rx timestamp.
typedef void (*capture_callback_t)(unsigned char* buf, uint32_t len, void* ctx);

// Returns the current time in microseconds since startup: steady_now_us() for a
// live radio, the replay clock (driven by the samples read) for a capture.
typedef uint64_t (*rx_clock_t)();

// Replays one or more raw 8-bit interleaved I/Q capture files (or a single
// stream from stdin when the list is empty). Time is taken from the number of
// samples read, so the reported timestamps are repeatable. For each chunk it
// invokes cb(buf, len, replay clock) and then report_detections(); it stops
// early when do_exit becomes true. Playback is paced to sample_rate to mimic a
// live capture, unless compiled with REPLAY_UNPACED.
void replay_capture(const std::vector<std::string>& files,
                    double sample_rate,
                    capture_callback_t cb,
                    const std::atomic<bool>& do_exit);
