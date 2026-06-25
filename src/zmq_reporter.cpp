#include "commons.hpp"

#include <format>
#include <iostream>
#include <string>

#include <zmq.hpp>

struct ZmqReporter::Impl {
    zmq::context_t context;
    zmq::socket_t publisher;

    Impl(int port) : context(1), publisher(context, zmq::socket_type::pub) {
        std::string address;
        std::format_to(std::back_inserter(address), "tcp://*:{}", port);
        publisher.bind(address);
        std::cout << "Listening on " << address << std::endl;
    }

    void send(const std::string& report) {
        std::cout << report << std::endl;
        publisher.send(zmq::buffer(report), zmq::send_flags::none);
    }
};

ZmqReporter::ZmqReporter(int port) : impl(std::make_unique<Impl>(port)) {}
ZmqReporter::~ZmqReporter() = default;

void ZmqReporter::on_status(uint64_t report_ts, float noise, float dc_offset, uint32_t frames_rx, uint32_t frames_processed) {
    impl->send(std::format("S {} {:.2f} {:.2f} {} {}", report_ts, noise, dc_offset, frames_rx, frames_processed));
}

void ZmqReporter::on_passing(uint64_t report_ts, const Passing& passing) {
    impl->send(std::format("P {} {} {} {:.2f} {} {}",
        report_ts,
        transponder_system_name(passing.transponder_type),
        passing.transponder_id,
        passing.rssi,
        passing.hits,
        passing.duration
    ));
}

void ZmqReporter::on_timesync(uint64_t report_ts, const TimeSync& timesync) {
    impl->send(std::format("T {} {} {} {}",
        report_ts,
        transponder_system_name(timesync.transponder_type),
        timesync.transponder_id,
        timesync.transponder_timestamp
    ));
}

void ZmqReporter::on_rc4_training_start(uint64_t report_ts, float rssi) {
    impl->send(std::format("L {} START {:.1f}", report_ts, rssi));
}

void ZmqReporter::on_rc4_training_interrupted(uint64_t report_ts) {
    impl->send(std::format("L {} INTERRUPTED", report_ts));
}

void ZmqReporter::on_rc4_training_done(uint64_t report_ts, uint32_t transponder_id, uint32_t payload_count) {
    impl->send(std::format("L {} DONE {} {}", report_ts, transponder_id, payload_count));
}

void ZmqReporter::on_rc4_training_reset(uint64_t report_ts) {
    impl->send(std::format("L {} RESET", report_ts));
}
