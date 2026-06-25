#include "commons.hpp"

#include <complex>
#include <format>
#include <iostream>
#include <string>
#include <vector>

using namespace std::chrono;

// --- OpenStintEngine ---

OpenStintEngine::OpenStintEngine(Reporter& reporter, RC4Registry& rc4_registry, bool monitor_mode, bool mode_sysclk)
    : startup_ts(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()),
      monitor_mode(monitor_mode),
      mode_sysclk(mode_sysclk),
      reporter(reporter),
      rc4_registry(rc4_registry) {
    rc4_registry.resync();
}

bool OpenStintEngine::process_frame(Frame* frame) {
    if (monitor_mode) {
        std::cout << "F " << *frame << std::endl;
    }

    const uint8_t *softbits = frame->bits();
    if (!softbits) {
        // preamble not found
        return false;
    }

    uint32_t transponder_id;
    switch (frame->transponder_protocol) {
        case TransponderProtocol::OpenStint:
        if (decoder.decode_openstint(softbits, &transponder_id)) {
            if (transponder_id < 10000000u) {
                passing_detector.append(frame, transponder_id);
            } else if ((transponder_id & 0x00A00000) == 0x00A00000) {
                uint32_t transponder_timestamp = (transponder_id & 0x000FFFFF);
                passing_detector.timesync(frame, transponder_timestamp);
            }
            return true;
        }
        break;
        case TransponderProtocol::RC3: {
            uint8_t status_code;
            if (decoder.decode_rc3(softbits, &transponder_id, &status_code)) {
                if (transponder_id >= 10000000) { // not a 7-digit transponder for sure
                    // check for known status/validation message (to track some statistics)
                    return ((status_code & 0x07) == 0);
                }
                // status byte:
                // https://www.rctech.net/forum/showpost.php?p=16244070&postcount=1171
                // RC4 hybrid and "recent" RC3 indicate status messages in lower 3 bits (0x07 mask)
                // Older RC3 indicate normal messages by setting all bits 1 (0xff)

                // Old AMBRc DP transponders send *transponder* frames with all status bits set (0xff);
                // unfortunately newer models can transmit RC3 status/validation messages the same way.
                // Let's build a block-list for such transponders.
                ambrc_blacklist.process(frame->timestamp, status_code, transponder_id);
                if (status_code == 0xff && !ambrc_blacklist.check_banned(transponder_id)) {
                    passing_detector.append(frame, transponder_id);
                } else if ((status_code & 0x07) == 0) { // not a status/validation message for sure
                    passing_detector.append(frame, transponder_id);
                }
                // at this point decoding was success; if status byte indicates
                // non-transponder message, it should not screw decoded statistics
                return true;
            }
        }
        break;
        case TransponderProtocol::RC4: {
            RC4Message msg(softbits);
            if (!msg.is_valid) { // fails validation
                return false;
            }
            if (rc4_registry.lookup(msg.payload, &transponder_id)) {
                passing_detector.append(frame, transponder_id);
                rc4_trainer.append(frame->timestamp, frame->rssi(), transponder_id, msg.payload);
                return true;
            }
            rc4_trainer.append(frame->timestamp, frame->rssi(), 0, msg.payload);
            return true;
        }
    }
    return false;
}

void OpenStintEngine::detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count) {
    const uint64_t timestamp = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() - startup_ts;

    // on USB hiccup, there might be a super-small buffer, which can not even fit
    // the preamble; these buffers should be dropped as bougus to prevent indexing
    // issues later on.
    if (sample_count < SymbolReader::reserve_buffer_size) {
        timecode += sample_count;
        return; // no meaningful work here
    }

    bool frame_detected = false;
    for (uint32_t idx=0; (idx+SAMPLES_PER_SYMBOL)<=sample_count; idx+=SAMPLES_PER_SYMBOL) {
        if (parse_mode == FRAME_SEEK) {
            const std::optional<DetectionResult> detected = frame_detector.process_baseband(samples+idx);
            if (detected) {
                parse_mode = FRAME_WAIT;
                frame_detected = true; // do not use this buffer for noisefloor calculation
                frame = Frame(
                    detected.value().first,
                    detected.value().second,
                    timestamp + (static_cast<uint64_t>(idx) * 1000000ull / SAMPLE_RATE), // "UL" on windows is 4 bytes :o
                    timecode + idx
                );
                // defer training by fseq_halflen symbols: the centered EQ needs the
                // trailing (future) symbols, which become ordinary past samples once
                // they arrive. timing stays anchored at this detection point.
                pending_trail = SymbolReader::fseq_halflen;
            }
        } else if (parse_mode == FRAME_WAIT) {
            // count the trailing symbols of the centered EQ window; once they are in,
            // train/read the preamble looking both back (lead) and ahead (trailing).
            if (--pending_trail == 0) {
                const int end = idx + SAMPLES_PER_SYMBOL;
                symbol_reader.train_preamble(&frame, samples, end, frame_detector.dc_offset());
                symbol_reader.read_preamble(&frame, samples, end, frame_detector.dc_offset());
                parse_mode = FRAME_FOUND;
            }
        } else if (parse_mode == FRAME_FOUND) {
            symbol_reader.read_symbol(&frame, samples+idx, frame_detector.dc_offset());
            if (symbol_reader.is_frame_complete(&frame)) {
                parse_mode = FRAME_SEEK;
                bool frame_processed = process_frame(&frame);
                rx_stats.register_frame(frame_processed);
            }
        }
    }

    // update global sample counter
    timecode += sample_count;

    // save a small section of the buffer
    // if there is a frame in the next buffer, and read_preamble() must
    // look back, here save the trailing section of the current buffer
    symbol_reader.update_reserve_buffer(samples, sample_count);

    // update counters for noise energy and dc offset
    if (frame_detected) {
        // there was an active frame in the buffer, do not update
        // statistics, as the received data messes with the
        // noise/dc-offset calculation
        frame_detector.reset_statistics_counters();
    } else {
        frame_detector.update_statistics();
        rx_stats.save_channel_characteristics(
            frame_detector.dc_offset(),
            frame_detector.noise_energy()
        );
    }
}

uint64_t OpenStintEngine::reporting_timestamp(uint64_t timestamp_us, uint64_t steady_now, uint64_t sysclk_now) {
    if (mode_sysclk) {
        return (sysclk_now - (steady_now - timestamp_us))/1000ul;
    } else {
        return timestamp_us/1000ul;
    }
}

void OpenStintEngine::report_detections() {
    const uint64_t now_sysclk = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    const uint64_t now_ts = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() - startup_ts;
    const uint64_t status_ts = reporting_timestamp(now_ts, now_ts, now_sysclk);

    // report status once a second
    if (rx_stats.reporting_due(now_ts)) {
        auto snap = rx_stats.snapshot();
        reporter.on_status(status_ts, snap.noise, snap.dc_offset, snap.frames_rx, snap.frames_processed);
        rx_stats.reset(now_ts);
    }

    std::vector<TimeSync> timesyncs = passing_detector.identify_timesyncs(500000l);
    for (const auto& time_sync : timesyncs) {
        reporter.on_timesync(
            reporting_timestamp(time_sync.timestamp, now_ts, now_sysclk),
            time_sync
        );
    }

    std::vector<Passing> passings = passing_detector.identify_passings(now_ts > 250000ul ? (now_ts-250000ul) : 0ul);
    for (const auto& passing : passings) {
        reporter.on_passing(
            reporting_timestamp(passing.timestamp, now_ts, now_sysclk),
            passing
        );
    }

    auto trainer_result = rc4_trainer.evaluate(now_ts);
    switch (trainer_result) {
        case RC4Trainer::EvaluationResult::START:
            reporter.on_rc4_training_start(status_ts, rc4_trainer.last_rssi());
            break;
        case RC4Trainer::EvaluationResult::INTERRUPED:
            reporter.on_rc4_training_interrupted(status_ts);
            break;
        case RC4Trainer::EvaluationResult::DONE: {
            uint32_t transponder_id = rc4_trainer.preferred_transponder_id();
            auto payloads = rc4_trainer.registry_payloads();
            auto [tsmin, tsmax] = rc4_trainer.buffer_timerange();
            auto detected_transponders = passing_detector.passings_between(TransponderSystem::AMB, tsmin, tsmax);
            if (transponder_id == 0 && detected_transponders.size() == 1) {
                transponder_id = detected_transponders.front();
            }
            transponder_id = rc4_registry.store(transponder_id, payloads);
            reporter.on_rc4_training_done(status_ts, transponder_id, static_cast<uint32_t>(payloads.size()));
        }
        break;
        case RC4Trainer::EvaluationResult::RESET:
            reporter.on_rc4_training_reset(status_ts);
            break;
        case RC4Trainer::EvaluationResult::NO_ACTION:
            break;
    }

    // re-sync rc4 transponder database
    rc4_registry.resync();
}
