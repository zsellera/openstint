#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include <wx/wx.h>

#include "passing.hpp"

// a reported passing, as kept by the GUI
struct PassingRecord {
    wxDateTime wall_time;     // local time of the passing point
    std::string type;         // OPN / AMB / VOS
    uint32_t transponder_id;
    float rssi;               // dBFS, strongest hit
    size_t hits;
    float mer;                // dB, as reported in the passing ("snr")
    uint64_t duration_us;     // 0 if not detected
    uint64_t timestamp_us;    // passing point, decoder timebase
    float noise_floor;        // dBFS, last noise floor reported before the passing's first hit
    std::vector<PassingHit> hit_list;
};

// A full-width, coloured status indicator (rounded box with centered text).
class StatusPill : public wxPanel {
public:
    explicit StatusPill(wxWindow* parent);
    void set(const wxString& text, const wxColour& colour, const wxString& tooltip = {});

private:
    void on_paint(wxPaintEvent&);

    wxString text;
    wxColour colour;
};

// Peak 64-symbol window energy of every Nth received buffer, as a scrolling line chart.
class EnergyChart : public wxPanel {
public:
    explicit EnergyChart(wxWindow* parent);
    void push(float dbfs);
    void clear();

private:
    void on_paint(wxPaintEvent&);

    static constexpr size_t capacity = 120; // points; ~25 s when every 32nd 6.5 ms buffer is measured
    std::deque<float> values;
};

// Hits of a single passing: time (relative to the passing point) vs. RSSI, coloured by MER.
class PassingChart : public wxPanel {
public:
    explicit PassingChart(wxWindow* parent);
    void show(const PassingRecord& record);
    void clear();

private:
    void on_paint(wxPaintEvent&);

    std::optional<PassingRecord> record;
};
