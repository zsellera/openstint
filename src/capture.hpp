#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Callback signature matching rtlsdr_read_async_cb_t, so the RTL-SDR binary can
// hand its existing rx_callback straight to the replayer. The HackRF and file
// binaries supply a small shim that reinterprets the raw bytes appropriately.
typedef void (*capture_callback_t)(unsigned char* buf, uint32_t len, void* ctx);

// Replays one or more raw 8-bit interleaved I/Q capture files (or a single
// stream from stdin when the list is empty), pacing playback to sample_rate so
// it mimics a live capture. For each chunk it invokes cb(buf, len, ctx) and
// then report_detections(); it stops early when do_exit becomes true.
void replay_capture(const std::vector<std::string>& files,
                    double sample_rate,
                    capture_callback_t cb,
                    void* ctx,
                    const std::atomic<bool>& do_exit);
