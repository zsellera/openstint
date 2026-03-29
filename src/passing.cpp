#include <cstdint>

#include <algorithm> 
#include <vector>
#include <cmath>

#include "passing.hpp"

#define REPORT_HIT_LIMIT 2
#define TRANSPONDER_DETECTION_MSG_LIMIT (1<<12)

// scipy.signal.firwin(11, 8, fs=128, window="hann")
static const std::vector<float> smoothing_fir = {
    0.00000000f, 0.01320163f, 0.0588375f,
    0.12796555f, 0.19141461f, 0.21716141f,
    0.19141461f, 0.12796555f, 0.0588375f,
    0.01320163f, 0.00000000f
};

uint64_t timecode_to_usec(uint64_t timecode) {
    return timecode * 1000000ul / SAMPLE_RATE;
}

TransponderSystem transponder_system(TransponderProtocol ttype) {
    switch (ttype) {
        case TransponderProtocol::OpenStint:
        return TransponderSystem::OpenStint;
        case TransponderProtocol::RC3:
        case TransponderProtocol::RC4:
        return TransponderSystem::AMB;
    }
}

std::string transponder_system_name(TransponderSystem tsys) {
    switch (tsys) {
        case TransponderSystem::OpenStint:
        return "OPN";
        case TransponderSystem::AMB:
        return "AMB";
    }
}

void PassingDetector::append(const Frame* frame, uint32_t transponder_id) {
    TransponderKey transponder_key = std::make_pair(transponder_system(frame->transponder_protocol), transponder_id);
    Detection d(frame->timestamp, frame->timecode, frame->rssi());
    
    std::lock_guard<std::mutex> lock(mutex);
    detections[transponder_key].push_back(std::move(d));
    if (detections[transponder_key].size() > TRANSPONDER_DETECTION_MSG_LIMIT) {
        // this is an optimalization / guard: if a car parks on the loop,
        // we could fill up the memory and things go get crazy. A transponder
        // sends on avg. ~700 messages a second. A sane limit must apply.
        detections[transponder_key].pop_front();
    }
}

void PassingDetector::timesync(const Frame* frame, uint32_t transponder_timestamp) {
    TimeSyncMsg ts(frame->timestamp, transponder_timestamp);
    std::lock_guard<std::mutex> lock(mutex);
    timesync_messages.push_back(std::move(ts));
}

// Linear interpolation similar to numpy.interp()
// x_new: output sample points (must be sorted)
// x: input sample points (must be sorted, increasing)
// y: input sample values
std::vector<float> interp(const std::vector<float>& x_new,
                          const std::vector<float>& x,
                          const std::vector<float>& y)
{
    std::vector<float> y_new(x_new.size());
    size_t j = 0;

    for (size_t i = 0; i < x_new.size(); i++) {
        float xi = x_new[i];

        // Clamp to range (like numpy's default behavior)
        if (xi <= x.front()) {
            y_new[i] = y.front();
            continue;
        }
        if (xi >= x.back()) {
            y_new[i] = y.back();
            continue;
        }

        // Find interval [x[j], x[j+1]] containing xi
        while (j < x.size() - 2 && x[j+1] < xi) {
            j++;
        }

        // Linear interpolation
        float t = (xi - x[j]) / (x[j+1] - x[j]);
        y_new[i] = y[j] + t * (y[j+1] - y[j]);
    }

    return y_new;
}

std::vector<float> filtfilt(const std::vector<float>& b,
                            const std::vector<float>& x)
{
    unsigned int n = x.size();
    unsigned int numtaps = b.size();
    unsigned int padlen = 3 * numtaps;  // scipy default

    // Create padded signal with reflected edges
    std::vector<float> padded(n + 2 * padlen);

    // Reflect left edge: 2*x[0] - x[padlen], ..., 2*x[0] - x[1]
    for (unsigned int i = 0; i < padlen; i++)
        padded[i] = 2 * x[0] - x[padlen - i];

    // Copy original signal
    std::copy(x.begin(), x.end(), padded.begin() + padlen);

    // Reflect right edge: 2*x[n-1] - x[n-2], ..., 2*x[n-1] - x[n-1-padlen]
    for (unsigned int i = 0; i < padlen; i++)
        padded[n + padlen + i] = 2 * x[n-1] - x[n - 2 - i];

    // Create filter
    firfilt_rrrf q = firfilt_rrrf_create((float*)b.data(), numtaps);

    // Forward pass
    std::vector<float> y1(padded.size());
    firfilt_rrrf_execute_block(q, padded.data(), padded.size(), y1.data());

    // Reverse
    std::reverse(y1.begin(), y1.end());

    // Reset filter state
    firfilt_rrrf_reset(q);

    // Backward pass
    std::vector<float> y2(y1.size());
    firfilt_rrrf_execute_block(q, y1.data(), y1.size(), y2.data());

    // Reverse back
    std::reverse(y2.begin(), y2.end());

    // Remove padding
    std::vector<float> result(n);
    std::copy(y2.begin() + padlen, y2.begin() + padlen + n, result.begin());

    firfilt_rrrf_destroy(q);
    return result;
}

struct Peak {
    size_t index;
    float value;
    float prominence;
};

std::vector<Peak> find_peaks(const std::vector<float>& y, float min_prominence = 1.0f)
{
    std::vector<Peak> peaks;

    // Find all local maxima
    for (size_t i = 1; i < y.size() - 1; i++) {
        if (y[i] > y[i-1] && y[i] > y[i+1]) {
            peaks.push_back({i, y[i], 0.0f});
        }
    }

    // Calculate prominence for each peak
    for (auto& peak : peaks) {
        // Extend left until we hit a higher peak or boundary
        float left_min = peak.value;
        for (size_t j = peak.index; j > 0; j--) {
            left_min = std::min(left_min, y[j]);
            if (y[j] > peak.value) break;
        }

        // Extend right until we hit a higher peak or boundary
        float right_min = peak.value;
        for (size_t j = peak.index; j < y.size(); j++) {
            right_min = std::min(right_min, y[j]);
            if (y[j] > peak.value) break;
        }

        // Prominence is height above the higher of the two valleys
        peak.prominence = peak.value - std::max(left_min, right_min);
    }

    // Filter by prominence
    std::vector<Peak> result;
    for (const auto& peak : peaks) {
        if (peak.prominence >= min_prominence)
            result.push_back(peak);
    }

    return result;
}

struct PassingPoint {
    uint64_t weighted_timestamp;
    float max_rssi;
    uint64_t duration;
};

// Calculate RSSI-weighted average timestamp for detections
PassingPoint weigthed_passing(const std::deque<Detection>& detections, float max_rssi) {
    float rssi_threshold = max_rssi - 6.0f;

    float weighted_sum = 0.0f;
    float weight_total = 0.0f;

    // in "system time" mode, milisecond-resolution epock is used
    // ms-resolution epoch is too large for floating point, and gets truncated
    // if the offset is removed, we can keep using a weighted average
    uint64_t offset = detections.front().timecode;

    for (const auto& d : detections) {
        if (d.rssi >= rssi_threshold) {
            auto p = std::pow(10.0, d.rssi/20.0);
            weighted_sum += static_cast<float>(d.timecode-offset) * p;
            weight_total += p;
        }
    }

    uint64_t timecode_average = static_cast<uint64_t>(weighted_sum / weight_total);
    uint64_t weighted_timestamp = detections.front().timestamp + timecode_to_usec(timecode_average);
    return {weighted_timestamp, max_rssi, 0};
}

std::tuple<std::vector<float>, uint64_t, uint64_t> resamp_uniform(const std::deque<Detection>& detections) {
    // normalize detection timecode to [0,1] interval
    uint64_t tc_min = detections.front().timecode;
    uint64_t tc_max = detections.back().timecode;
    uint64_t tc_diff = tc_max - tc_min;
    std::vector<float> t_sample(detections.size());
    std::transform(
        detections.begin(), detections.end(),
        t_sample.begin(),
        [tc_min, tc_diff](const Detection d) { return static_cast<float>(d.timecode - tc_min) / static_cast<float>(tc_diff); }
    );

    // Create grid of 128+1 points in [0,1]
    std::vector<float> t_uniform(129);
    for (int i = 0; i <= 128; i++) {
        t_uniform[i] = i / 128.0f;
    }

    // Interpolate RSSI levels to a uniform time intervals
    std::vector<float> y_irregular(detections.size());
    std::transform(
        detections.begin(), detections.end(),
        y_irregular.begin(),
        [](const Detection d) { return d.rssi; }
    );
    auto y_uniform = interp(t_uniform, t_sample, y_irregular);

    return {y_uniform, tc_min, tc_diff};
}

// Returns interpolated index where values first exceed threshold v.
float first_crossing(const std::vector<float>& data, float v) {
    if (data[0] >= v) {
        return 0.0f;
    }
    for (size_t i = 1; i < data.size(); i++) {
        if (data[i] >= v) {
            float t = (v - data[i-1]) / (data[i] - data[i-1]);
            return (i-1) + t;
        }
    }
    return 0.f;
}

float last_crossing(const std::vector<float>& data, float v) {
    int k = data.size() - 1;
    if (data[k] >= v) {
        return static_cast<float>(k);
    }
    for (size_t i = k-1; i > 0; i--) {
        if (data[i] >= v) {
            float t = (v - data[i+1]) / (data[i] - data[i+1]);
            return (i+1) - t;
        }
    }
    return static_cast<float>(k);
}

PassingPoint compute_passing_point(const std::deque<Detection>& detections) {
    // Find the maximum RSSI
    const auto max_it = std::max_element(
        detections.begin(),
        detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.rssi < b.rssi;
        });
    float max_rssi = max_it->rssi;

    // if just a few hits were received, do a weighted average of
    // peak points to find the passing point
    if (detections.size() < 16) {
        return weigthed_passing(detections, max_rssi);
    }

    // there are enough datapoints to pattern match on the waveform; first resample to a uniform timegrid
    const auto [y_uniform, tc_start, tc_duration] = resamp_uniform(detections);
    // if the transponder was placed paralel to the detection antenna, and the antenna was close,
    // there are two nulls right when the transponder passed over the loop wires
    std::vector<float> y_peaking(129);
    std::transform(y_uniform.begin(), y_uniform.end(), y_peaking.begin(), std::negate<float>{});
    auto rssi_dips = find_peaks(y_peaking, 3.0f /* prominence in dB */);
    if (rssi_dips.size() == 3) {
        auto pass_duration = (rssi_dips[2].index - rssi_dips[0].index) * tc_duration / 128ul;
        auto pass_timecode = tc_start + rssi_dips[0].index*tc_duration/128ul + pass_duration/2;
        return {
            timecode_to_usec(pass_timecode),
            max_rssi,
            timecode_to_usec(pass_duration)
        };
    }
    // no dual dips were detected, try find double peaks on a smoothed transition waveform
    auto y_smoothed = filtfilt(smoothing_fir, y_uniform);
    auto rssi_peaks = find_peaks(y_smoothed, 1.0f /* dB */);
    if (rssi_peaks.size() == 2) {
        auto pass_duration = (rssi_peaks[1].index - rssi_peaks[0].index) * tc_duration / 128ul;
        auto pass_timecode = tc_start + rssi_peaks[0].index*tc_duration/128ul + pass_duration/2;
        return {
            timecode_to_usec(pass_timecode),
            max_rssi,
            timecode_to_usec(pass_duration)
        };
    }
    // fall back to default:
    // find the first and last time when the waveform passed the peak-6dB mark,
    // the passing point should be inbetween
    float max_smoothed = *std::max_element(y_smoothed.begin(), y_smoothed.end());
    float idx_first = first_crossing(y_smoothed, max_smoothed-6.0f);
    float idx_last = last_crossing(y_smoothed, max_smoothed-6.0f);
    float pass_width = idx_last-idx_first;
    uint64_t pass_timecode = tc_start + static_cast<uint64_t>((idx_first+pass_width/2.0f)/128.0f*tc_duration);

    return {timecode_to_usec(pass_timecode), max_rssi, 0 };
}

Passing create_passing(TransponderKey transponder_key, const std::deque<Detection>& detections) {
    PassingPoint stats = compute_passing_point(detections);
    Passing p = {
        .timestamp = stats.weighted_timestamp,
        .transponder_type = transponder_key.first,
        .transponder_id = transponder_key.second,
        .rssi = stats.max_rssi,
        .hits = detections.size(),
        .duration = stats.duration
    };
    return p;
}

std::vector<Passing> PassingDetector::identify_passings(uint64_t deadline) {
    std::lock_guard<std::mutex> lock(mutex);

    // collect passings:
    std::vector<Passing> passings;
    std::vector<TransponderKey> erasable_entries;
    for (const auto& [transponder_key, detections] : detections) {
        if (!detections.empty() && detections.back().timestamp <= deadline) {
            Passing p = create_passing(transponder_key, detections);
            erasable_entries.push_back(transponder_key);
            if (p.hits >= REPORT_HIT_LIMIT) {
                passings.push_back(std::move(p));
            }
        }
    }

    // delete old or invalid detections:
    for (TransponderKey key : erasable_entries) {
        detections.erase(key);
    }

    return passings;
}

std::vector<TimeSync> PassingDetector::identify_timesyncs(uint64_t margin) {
    std::vector<TimeSync> timesyncs;

    std::lock_guard<std::mutex> lock(mutex);
    // verify all timesync messages can belong only to a single transponder
    for (const auto& ts_msg : timesync_messages) {
        TransponderKey matching_transponder;
        int matching_transponder_count = 0;

        for (const auto& [transponder_key, detection_vec] : detections) {
            if (detection_vec.empty()) { continue; }
            // margin makes sure two consequtive passings do not leave a timesync inbetween
            bool front_ok = (detection_vec.front().timestamp - margin) < ts_msg.decoder_timestamp;
            bool back_ok = (detection_vec.back().timestamp + margin) > ts_msg.decoder_timestamp;
            if  (front_ok && back_ok) {
                ++matching_transponder_count;
                matching_transponder = transponder_key;
            }
        }
        if (matching_transponder_count == 1) {
            TimeSync ts = {
                .timestamp = ts_msg.decoder_timestamp,
                .transponder_type = matching_transponder.first,
                .transponder_id = matching_transponder.second,
                .transponder_timestamp = ts_msg.transponder_timestamp
            };
            timesyncs.push_back(std::move(ts));
        }
    }

    // erase all processed timesync messages
    timesync_messages.clear();

    return timesyncs;
}

std::vector<uint32_t> PassingDetector::passings_between(TransponderSystem tsys, uint64_t from, uint64_t until) {
    std::vector<uint32_t> transponders;
    for (const auto& [transponder_key, detection_vec] : detections) {
        if (transponder_key.first == tsys &&
            detection_vec.front().timestamp <= until &&
            detection_vec.back().timestamp >= from) {
                transponders.push_back(transponder_key.second);
        }
    }
    return transponders;
}
