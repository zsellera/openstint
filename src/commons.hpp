#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "frame.hpp"
#include "passing.hpp"
#include "counters.hpp"
#include "rc4.hpp"

#define DEFAULT_ZEROMQ_PORT 5556

struct CommonOptions {
    int zmq_port = DEFAULT_ZEROMQ_PORT;
    bool monitor_mode = false;
    bool mode_sysclk = false;
    std::string storage_dir = ".";
};

bool parse_common_arguments(CommonOptions& opts, int& i, const int argc, const std::string& arg, char** argv);

class Reporter {
public:
    virtual ~Reporter() = default;
    virtual void on_status(uint64_t /*report_ts*/, float /*noise*/, float /*dc_offset*/, uint32_t /*frames_rx*/, uint32_t /*frames_processed*/) {}
    virtual void on_passing(uint64_t /*report_ts*/, const Passing& /*passing*/) {}
    virtual void on_timesync(uint64_t /*report_ts*/, const TimeSync& /*timesync*/) {}
    virtual void on_rc4_training_start(uint64_t /*report_ts*/, float /*rssi*/) {}
    virtual void on_rc4_training_interrupted(uint64_t /*report_ts*/) {}
    virtual void on_rc4_training_done(uint64_t /*report_ts*/, uint32_t /*transponder_id*/, uint32_t /*payload_count*/) {}
    virtual void on_rc4_training_reset(uint64_t /*report_ts*/) {}
};

class CallbackReporter : public Reporter {
    std::function<void(const Passing&)> passing_cb;
    std::function<void(const TimeSync&)> timesync_cb;
    std::function<void(uint64_t, float, float, uint32_t, uint32_t)> status_cb;
    std::function<void(uint64_t, float)> rc4_training_start_cb;
    std::function<void(uint64_t)> rc4_training_interrupted_cb;
    std::function<void(uint64_t, uint32_t, uint32_t)> rc4_training_done_cb;
    std::function<void(uint64_t)> rc4_training_reset_cb;

public:
    void set_on_passing(std::function<void(const Passing&)> cb) { passing_cb = std::move(cb); }
    void set_on_timesync(std::function<void(const TimeSync&)> cb) { timesync_cb = std::move(cb); }
    void set_on_status(std::function<void(uint64_t, float, float, uint32_t, uint32_t)> cb) { status_cb = std::move(cb); }
    void set_on_rc4_training_start(std::function<void(uint64_t, float)> cb) { rc4_training_start_cb = std::move(cb); }
    void set_on_rc4_training_interrupted(std::function<void(uint64_t)> cb) { rc4_training_interrupted_cb = std::move(cb); }
    void set_on_rc4_training_done(std::function<void(uint64_t, uint32_t, uint32_t)> cb) { rc4_training_done_cb = std::move(cb); }
    void set_on_rc4_training_reset(std::function<void(uint64_t)> cb) { rc4_training_reset_cb = std::move(cb); }

    void on_status(uint64_t report_ts, float noise, float dc_offset, uint32_t frames_rx, uint32_t frames_processed) override { if (status_cb) status_cb(report_ts, noise, dc_offset, frames_rx, frames_processed); }
    void on_passing(uint64_t, const Passing& passing) override { if (passing_cb) passing_cb(passing); }
    void on_timesync(uint64_t, const TimeSync& timesync) override { if (timesync_cb) timesync_cb(timesync); }
    void on_rc4_training_start(uint64_t report_ts, float rssi) override { if (rc4_training_start_cb) rc4_training_start_cb(report_ts, rssi); }
    void on_rc4_training_interrupted(uint64_t report_ts) override { if (rc4_training_interrupted_cb) rc4_training_interrupted_cb(report_ts); }
    void on_rc4_training_done(uint64_t report_ts, uint32_t transponder_id, uint32_t payload_count) override { if (rc4_training_done_cb) rc4_training_done_cb(report_ts, transponder_id, payload_count); }
    void on_rc4_training_reset(uint64_t report_ts) override { if (rc4_training_reset_cb) rc4_training_reset_cb(report_ts); }
};

class ZmqReporter : public Reporter {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    explicit ZmqReporter(int port);
    ~ZmqReporter();

    void on_status(uint64_t report_ts, float noise, float dc_offset, uint32_t frames_rx, uint32_t frames_processed) override;
    void on_passing(uint64_t report_ts, const Passing& passing) override;
    void on_timesync(uint64_t report_ts, const TimeSync& timesync) override;
    void on_rc4_training_start(uint64_t report_ts, float rssi) override;
    void on_rc4_training_interrupted(uint64_t report_ts) override;
    void on_rc4_training_done(uint64_t report_ts, uint32_t transponder_id, uint32_t payload_count) override;
    void on_rc4_training_reset(uint64_t report_ts) override;
};

class OpenStintEngine {
    TransponderDecoder decoder;
    FrameDetector frame_detector;
    SymbolReader symbol_reader;
    Frame frame;
    PassingDetector passing_detector;
    RxStatistics rx_stats;
    RC4Trainer rc4_trainer;
    AmbRcBlacklist ambrc_blacklist;

    enum { FRAME_SEEK, FRAME_WAIT, FRAME_FOUND } parse_mode = FRAME_SEEK;
    int pending_trail = 0;
    uint64_t startup_ts;
    uint64_t timecode = 0;
    bool monitor_mode;
    bool mode_sysclk;

    Reporter& reporter;
    RC4Registry& rc4_registry;

    bool process_frame(Frame* frame);
    uint64_t reporting_timestamp(uint64_t timestamp_us, uint64_t steady_now, uint64_t sysclk_now);

public:
    OpenStintEngine(Reporter& reporter, RC4Registry& rc4_registry, bool monitor_mode = false, bool mode_sysclk = false);

    void detect_frames(const std::complex<int8_t>* samples, std::size_t sample_count);
    void report_detections();
};
