#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "capture.hpp"


// number of raw bytes (2 per IQ sample) read per chunk, matching the RTL-SDR
// async read buffer so the processing loop sees familiar chunk sizes.
static const size_t CHUNK_BYTES = 32768;

void replay_capture(const std::vector<std::string>& files,
                    double sample_rate,
                    capture_callback_t cb,
                    void* ctx,
                    const std::atomic<bool>& do_exit,
                    report_callback_t report_cb) {
    std::vector<uint8_t> read_buf(CHUNK_BYTES);

    // one chunk worth of complex samples corresponds to this much real time,
    // which we use to pace the replay to mimic a real-life capture.
    using clock = std::chrono::steady_clock;
    clock::time_point next_chunk = clock::now();

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

            // hand the raw chunk to the device-specific converter, then drain
            // whatever frames it produced.
            cb(read_buf.data(), byte_count, ctx);
            if (report_cb) report_cb();

            uint32_t sample_count = byte_count / 2;
            next_chunk += std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(sample_count / sample_rate));
            std::this_thread::sleep_until(next_chunk);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (report_cb) report_cb();
    }
}
