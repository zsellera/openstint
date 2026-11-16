#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "capture.hpp"
#include "commons.hpp"


// number of raw bytes (2 per IQ sample) read per chunk, matching the RTL-SDR
// async read buffer so the processing loop sees familiar chunk sizes.
static const size_t CHUNK_BYTES = 32768;

// silence appended to the end of each file, longer than the passing deadline
static const double REPLAY_FLUSH_SEC = 0.5;

// the replay clock: time is the number of samples read so far, so the
// reported timestamps are repeatable, independent of the host's clocks.
static uint64_t replay_samples_read = 0;
static double replay_sample_rate = 1.0;

static uint64_t replay_now_us() {
    return static_cast<uint64_t>(static_cast<double>(replay_samples_read) * 1e6 / replay_sample_rate);
}

void replay_capture(const std::vector<std::string>& files,
                    double sample_rate,
                    capture_callback_t cb,
                    const std::atomic<bool>& do_exit) {
    std::vector<uint8_t> read_buf(CHUNK_BYTES);

    replay_samples_read = 0;
    replay_sample_rate = sample_rate;

#ifndef REPLAY_UNPACED
    // one chunk worth of complex samples corresponds to this much real time,
    // which we use to pace the replay to mimic a real-life capture.
    using clock = std::chrono::steady_clock;
    clock::time_point next_chunk = clock::now();
#endif

    // build the list of streams to replay; an empty list means read stdin
    std::vector<std::string> sources = files;
    bool use_stdin = sources.empty();
    if (use_stdin) {
        sources.push_back("<stdin>");
    }

    for (const std::string& source : sources) {
        if (do_exit) {
            break;
        }

        std::ifstream file_in;
        std::istream* in = &std::cin;
        if (!use_stdin) {
            file_in.open(source, std::ios::binary);
            if (!file_in) {
                std::cerr << "Failed to open '" << source << "'\n";
                continue;
            }
            in = &file_in;
        }

        std::cout << "Replaying '" << source << "'\n";

        while (!do_exit) {
            in->read(reinterpret_cast<char*>(read_buf.data()), CHUNK_BYTES);
            std::streamsize n_read = in->gcount();
            if (n_read <= 0) {
                break;
            }

            // each IQ sample is two bytes; drop a trailing odd byte if any
            uint32_t byte_count = (static_cast<uint32_t>(n_read) / 2) * 2;
            if (byte_count == 0) {
                break;
            }
            uint32_t sample_count = byte_count / 2;

            // hand the raw chunk to the device-specific converter (the clock reads
            // the time of the chunk's first sample), then drain the frames it produced.
            cb(read_buf.data(), byte_count, reinterpret_cast<void*>(&replay_now_us));
            replay_samples_read += sample_count;
            report_detections(replay_now_us());

#ifndef REPLAY_UNPACED
            next_chunk += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(sample_count / sample_rate));
            std::this_thread::sleep_until(next_chunk);
#endif
        }

        // flush: advance the replay clock past the passing deadline so the
        // transponders still on the loop at the end of the file get reported.
        // The replay clock keeps running from here into the next file.
        replay_samples_read += static_cast<uint64_t>(sample_rate * REPLAY_FLUSH_SEC);
        report_detections(replay_now_us());
    }
}
