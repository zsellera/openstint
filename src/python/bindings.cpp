#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>

#include <complex>
#include <memory>
#include <vector>
#include <iostream>

#include "commons.hpp"

namespace py = pybind11;

struct PyEngine {
    std::shared_ptr<CallbackReporter> reporter;
    std::shared_ptr<RC4FileBasedRegistry> registry;
    std::unique_ptr<OpenStintEngine> engine;

    PyEngine(std::shared_ptr<CallbackReporter> r, std::shared_ptr<RC4FileBasedRegistry> reg)
        : reporter(std::move(r)), registry(std::move(reg)),
          engine(std::make_unique<OpenStintEngine>(*reporter, *registry)) {}

    void detect_frames(py::array samples) {
        auto arr = samples.cast<py::array_t<std::complex<double>>>();
        auto buf = arr.unchecked<1>();
        const std::size_t n = buf.shape(0);

        std::vector<std::complex<int8_t>> converted(n);
        for (std::size_t i = 0; i < n; i++) {
            converted[i] = {
                static_cast<int8_t>(std::clamp(buf(i).real() * 127.5 - 0.5, -128.0, 127.0)),
                static_cast<int8_t>(std::clamp(buf(i).imag() * 127.5 - 0.5, -128.0, 127.0))
            };
        }

        py::gil_scoped_release release;
        engine->detect_frames(converted.data(), n);
    }

    void detect_frames_raw(py::buffer buf) {
        py::buffer_info info = buf.request();
        auto* ptr = reinterpret_cast<const uint8_t*>(info.ptr);
        std::size_t count = static_cast<std::size_t>(info.size) / 2;

        std::vector<std::complex<int8_t>> converted(count);
        for (std::size_t i = 0; i < count; i++) {
            converted[i] = {
                static_cast<int8_t>(ptr[2 * i]     - 128),
                static_cast<int8_t>(ptr[2 * i + 1] - 128)
            };
        }

        py::gil_scoped_release release;
        engine->detect_frames(converted.data(), count);
    }

    void report_detections() {
        engine->report_detections();
    }
};

PYBIND11_MODULE(openstint, m) {
    m.doc() = "OpenStint transponder decoder";

    py::enum_<TransponderSystem>(m, "TransponderSystem")
        .value("OpenStint", TransponderSystem::OpenStint)
        .value("AMB", TransponderSystem::AMB);

    py::enum_<TransponderProtocol>(m, "TransponderProtocol")
        .value("OpenStint", TransponderProtocol::OpenStint)
        .value("RC3", TransponderProtocol::RC3)
        .value("RC4", TransponderProtocol::RC4);

    py::class_<Passing>(m, "Passing")
        .def_readonly("timestamp", &Passing::timestamp)
        .def_readonly("transponder_system", &Passing::transponder_type)
        .def_readonly("transponder_id", &Passing::transponder_id)
        .def_readonly("rssi", &Passing::rssi)
        .def_readonly("hits", &Passing::hits)
        .def_readonly("duration", &Passing::duration)
        .def("__repr__", [](const Passing& p) {
            return "<Passing id=" + std::to_string(p.transponder_id)
                + " rssi=" + std::to_string(p.rssi)
                + " hits=" + std::to_string(p.hits) + ">";
        });

    py::class_<TimeSync>(m, "TimeSync")
        .def_readonly("timestamp", &TimeSync::timestamp)
        .def_readonly("transponder_system", &TimeSync::transponder_type)
        .def_readonly("transponder_id", &TimeSync::transponder_id)
        .def_readonly("transponder_timestamp", &TimeSync::transponder_timestamp);

    py::class_<CallbackReporter, std::shared_ptr<CallbackReporter>>(m, "Reporter")
        .def(py::init<>())
        .def("on_passing", [](CallbackReporter& self, py::function callback) {
            self.set_on_passing([cb = std::move(callback)](const Passing& p) {
                py::gil_scoped_acquire gil;
                cb(p);
            });
        })
        .def("on_timesync", [](CallbackReporter& self, py::function callback) {
            self.set_on_timesync([cb = std::move(callback)](const TimeSync& t) {
                py::gil_scoped_acquire gil;
                cb(t);
            });
        })
        .def("on_status", [](CallbackReporter& self, py::function callback) {
            self.set_on_status([cb = std::move(callback)](uint64_t report_ts, float noise, float dc_offset, uint32_t frames_rx, uint32_t frames_processed) {
                py::gil_scoped_acquire gil;
                cb(report_ts, noise, dc_offset, frames_rx, frames_processed);
            });
        })
        .def("on_rc4_training_start", [](CallbackReporter& self, py::function callback) {
            self.set_on_rc4_training_start([cb = std::move(callback)](uint64_t report_ts, float rssi) {
                py::gil_scoped_acquire gil;
                cb(report_ts, rssi);
            });
        })
        .def("on_rc4_training_interrupted", [](CallbackReporter& self, py::function callback) {
            self.set_on_rc4_training_interrupted([cb = std::move(callback)](uint64_t report_ts) {
                py::gil_scoped_acquire gil;
                cb(report_ts);
            });
        })
        .def("on_rc4_training_done", [](CallbackReporter& self, py::function callback) {
            self.set_on_rc4_training_done([cb = std::move(callback)](uint64_t report_ts, uint32_t transponder_id, uint32_t payload_count) {
                py::gil_scoped_acquire gil;
                cb(report_ts, transponder_id, payload_count);
            });
        })
        .def("on_rc4_training_reset", [](CallbackReporter& self, py::function callback) {
            self.set_on_rc4_training_reset([cb = std::move(callback)](uint64_t report_ts) {
                py::gil_scoped_acquire gil;
                cb(report_ts);
            });
        });

    py::class_<RC4Registry, std::shared_ptr<RC4Registry>>(m, "RC4Registry")
        .def(py::init<>());

    py::class_<RC4FileBasedRegistry, RC4Registry, std::shared_ptr<RC4FileBasedRegistry>>(m, "RC4FileBasedRegistry")
        .def(py::init<std::string>(), py::arg("directory") = ".");

    py::class_<PyEngine>(m, "Engine")
        .def(py::init<std::shared_ptr<CallbackReporter>, std::shared_ptr<RC4FileBasedRegistry>>(),
             py::arg("reporter"), py::arg("registry"))
        .def("detect_frames", &PyEngine::detect_frames, py::arg("samples"))
        .def("detect_frames_raw", &PyEngine::detect_frames_raw, py::arg("buffer"))
        .def("report_detections", &PyEngine::report_detections);
}
