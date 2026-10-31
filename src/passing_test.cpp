// Checks the passing point of parked transponders: a plateau followed by (part of) a real pass.
// Usage: passing_test <testdata dir>

#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>

#include "passing.hpp"

static const uint64_t FRAME_GAP = SAMPLE_RATE / 700; // a transponder sends ~700 frames a second

static void push(std::deque<Detection>& d, uint64_t timecode, float rssi) {
    d.emplace_back(timecode * 1000000ull / SAMPLE_RATE, timecode, rssi, 15.0f);
}

static std::deque<Detection> load(const std::string& path) {
    std::deque<Detection> d;
    std::ifstream f(path);
    assert(f && "missing fixture");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        uint64_t tc;
        float rssi;
        std::istringstream(line) >> tc >> rssi;
        push(d, tc, rssi);
    }
    return d;
}

// parked for 1.5 s at the given level, with +-0.5 dB of ripple; returns the timecode after it
static uint64_t plateau(std::deque<Detection>& d, float level) {
    uint64_t tc = 0;
    for (int i = 0; i < 1050; i++, tc += FRAME_GAP) {
        push(d, tc, level + ((i % 3) - 1) * 0.5f);
    }
    return tc;
}

// the pass from frame `from` on, shifted to start at timecode `at`
static void append_pass(std::deque<Detection>& d, const std::deque<Detection>& pass, size_t from, uint64_t at) {
    for (size_t i = from; i < pass.size(); i++) {
        push(d, at + pass[i].timecode - pass[from].timecode, pass[i].rssi);
    }
}

static void expect_near(const char* name, uint64_t got, uint64_t want, uint64_t tolerance) {
    long long diff = static_cast<long long>(got) - static_cast<long long>(want);
    std::printf("%-28s got %8llu us, want %8llu us (%+lld)\n", name,
        (unsigned long long)got, (unsigned long long)want, diff);
    assert(std::llabs(diff) <= static_cast<long long>(tolerance));
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto pass = load(std::string(argv[1]) + "/pass_twin_peaks.txt");
    const PassingPoint regular = compute_passing_point(pass);
    assert(regular.duration > 0); // the twin peaks are found

    // (a) parked in front of the loop, then drove across it: same center as a regular pass
    {
        std::deque<Detection> d;
        uint64_t start = plateau(d, pass.front().rssi);
        append_pass(d, pass, 0, start);
        const PassingPoint p = compute_passing_point(d);
        expect_near("parked in front, center", p.weighted_timestamp, start * 1000000ull / SAMPLE_RATE + regular.weighted_timestamp, 5000);
        expect_near("parked in front, duration", p.duration, regular.duration, 5000);
    }

    // (b) parked on the loop, between the wires: only the exit wire is crossed
    {
        size_t mid = 0;
        while (pass[mid].timestamp < regular.weighted_timestamp) mid++;
        std::deque<Detection> d;
        uint64_t start = plateau(d, pass[mid].rssi);
        append_pass(d, pass, mid, start);
        const PassingPoint p = compute_passing_point(d);
        uint64_t exit_wire = start * 1000000ull / SAMPLE_RATE + regular.weighted_timestamp - pass[mid].timestamp + regular.duration / 2;
        expect_near("parked on loop, exit wire", p.weighted_timestamp, exit_wire, 10000);
        assert(p.duration == 0);
    }

    // (c) no wire to be seen, the signal just fades: last frame within 3 dB of the plateau
    {
        std::deque<Detection> d;
        uint64_t tc = plateau(d, -30.0f);
        uint64_t last_on_level = 0;
        for (int i = 0; i < 100; i++, tc += FRAME_GAP) {
            float rssi = -30.0f - 0.2f * i;
            if (rssi >= -33.0f) last_on_level = tc;
            push(d, tc, rssi);
        }
        const PassingPoint p = compute_passing_point(d);
        expect_near("fading, last on level", p.weighted_timestamp, last_on_level * 1000000ull / SAMPLE_RATE, 0);
        assert(p.duration == 0);
    }

    // a slow crawl is never flat, so it is not parked: timed at the middle like any pass
    {
        std::deque<Detection> d;
        uint64_t tc = 0;
        for (int i = 0; i <= 1400; i++, tc += FRAME_GAP) {
            push(d, tc, -20.0f - std::fabs(i - 700) * 0.03f);
        }
        const PassingPoint p = compute_passing_point(d);
        expect_near("slow crawl, middle", p.weighted_timestamp, 700 * FRAME_GAP * 1000000ull / SAMPLE_RATE, 10000);
    }

    // slow arrival with a flat top at the entry wire, then parked lower: the parked plateau counts
    {
        std::deque<Detection> d;
        uint64_t tc = 0;
        for (int i = 0; i <= 900; i++, tc += FRAME_GAP) {
            push(d, tc, -20.0f - std::fabs(i - 700) * 0.03f);
        }
        for (int i = 0; i < 1050; i++, tc += FRAME_GAP) {
            push(d, tc, -26.0f + ((i % 3) - 1) * 0.5f);
        }
        uint64_t last_on_level = 0;
        for (int i = 0; i < 100; i++, tc += FRAME_GAP) {
            float rssi = -26.0f - 0.2f * i;
            if (rssi >= -29.0f) last_on_level = tc;
            push(d, tc, rssi);
        }
        const PassingPoint p = compute_passing_point(d);
        expect_near("slow arrival, then parked", p.weighted_timestamp, last_on_level * 1000000ull / SAMPLE_RATE, 0);
    }

    std::printf("ok\n");
    return 0;
}
