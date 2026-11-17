#include "charts.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>

namespace {

const wxColour BACKGROUND(28, 28, 30);
const wxColour PLOT_BACKGROUND(20, 20, 22);
const wxColour BELOW_NOISE_BACKGROUND(40, 34, 52); // passing graph, under the noise floor
const wxColour NOISE_TEXT(150, 135, 185);
const wxColour GRID(58, 58, 62);
const wxColour AXIS_TEXT(170, 170, 175);
const wxColour TEXT(225, 225, 230);

struct ColourStop {
    double value;
    wxColour colour;
};

wxColour gradient(const std::vector<ColourStop>& stops, double v) {
    if (v <= stops.front().value) {
        return stops.front().colour;
    }
    for (size_t i = 1; i < stops.size(); ++i) {
        if (v <= stops[i].value) {
            const auto& a = stops[i - 1];
            const auto& b = stops[i];
            const double t = (v - a.value) / (b.value - a.value);
            auto mix = [t](unsigned char x, unsigned char y) {
                return static_cast<unsigned char>(std::lround(x + t * (y - x)));
            };
            return wxColour(mix(a.colour.Red(), b.colour.Red()),
                            mix(a.colour.Green(), b.colour.Green()),
                            mix(a.colour.Blue(), b.colour.Blue()));
        }
    }
    return stops.back().colour;
}

// below -40 dBFS dark blue, above -5 dBFS red, a blue-cyan-yellow-red ramp inbetween
wxColour energy_colour(double dbfs) {
    static const std::vector<ColourStop> stops = {
        {-40.0, wxColour(25, 45, 150)},
        {-28.0, wxColour(0, 170, 220)},
        {-16.0, wxColour(235, 205, 40)},
        {-5.0, wxColour(230, 40, 40)},
    };
    return gradient(stops, dbfs);
}

// below 3 dB red, above 9 dB green, through yellow inbetween
wxColour mer_colour(double mer) {
    static const std::vector<ColourStop> stops = {
        {3.0, wxColour(230, 50, 50)},
        {6.0, wxColour(235, 200, 40)},
        {9.0, wxColour(60, 200, 90)},
    };
    return gradient(stops, mer);
}

double nice_step(double span, int target_ticks) {
    const double raw = span / target_ticks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double n = raw / magnitude;
    const double step = n < 1.5 ? 1.0 : n < 3.0 ? 2.0 : n < 7.0 ? 5.0 : 10.0;
    return step * magnitude;
}

wxFont small_font(wxWindow* w) {
    wxFont f = w->GetFont();
    f.SetPointSize(std::max(7, f.GetPointSize() - 2));
    return f;
}

void draw_text_right(wxDC& dc, const wxString& s, wxCoord right, wxCoord ycenter) {
    const wxSize e = dc.GetTextExtent(s);
    dc.DrawText(s, right - e.x, ycenter - e.y / 2);
}

void draw_text_centered(wxDC& dc, const wxString& s, wxCoord xcenter, wxCoord top) {
    const wxSize e = dc.GetTextExtent(s);
    dc.DrawText(s, xcenter - e.x / 2, top);
}

} // namespace

// ---------------------------------------------------------------------------
// StatusPill
// ---------------------------------------------------------------------------

StatusPill::StatusPill(wxWindow* parent) : wxPanel(parent, wxID_ANY), colour(120, 120, 128) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, GetCharHeight() + FromDIP(10)));
    Bind(wxEVT_PAINT, &StatusPill::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
}

void StatusPill::set(const wxString& t, const wxColour& c, const wxString& tooltip) {
    text = t;
    colour = c;
    SetToolTip(tooltip.empty() ? t : tooltip);
    Refresh();
}

void StatusPill::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC pdc(this);
    wxGCDC dc(pdc);
    const wxSize size = GetClientSize();

    // blend into whatever the parent (static box) paints
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(colour));
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(5));

    wxFont bold = GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    dc.SetFont(bold);
    dc.SetTextForeground(*wxWHITE);
    const int pad = FromDIP(6);
    const wxString shown = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, size.x - 2 * pad);
    const wxSize e = dc.GetTextExtent(shown);
    dc.DrawText(shown, (size.x - e.x) / 2, (size.y - e.y) / 2);
}

// ---------------------------------------------------------------------------
// EnergyChart
// ---------------------------------------------------------------------------

static constexpr double ENERGY_MIN_DB = -60.0;
static constexpr double ENERGY_MAX_DB = 5.0;

EnergyChart::EnergyChart(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(FromDIP(wxSize(-1, 150)));
    Bind(wxEVT_PAINT, &EnergyChart::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
}

void EnergyChart::push(float dbfs) {
    values.push_back(dbfs);
    while (values.size() > capacity) {
        values.pop_front();
    }
    Refresh();
}

void EnergyChart::clear() {
    values.clear();
    Refresh();
}

void EnergyChart::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC pdc(this);
    wxGCDC dc(pdc);
    const wxSize size = GetClientSize();

    dc.SetBackground(wxBrush(BACKGROUND));
    dc.Clear();
    dc.SetFont(small_font(this));

    const int left = FromDIP(36), right = FromDIP(8), top = FromDIP(8), bottom = FromDIP(8);
    const wxRect plot(left, top, size.x - left - right, size.y - top - bottom);
    if (plot.width < 20 || plot.height < 20) {
        return;
    }
    auto to_y = [&](double db) {
        db = std::clamp(db, ENERGY_MIN_DB, ENERGY_MAX_DB);
        return plot.y + (ENERGY_MAX_DB - db) / (ENERGY_MAX_DB - ENERGY_MIN_DB) * plot.height;
    };

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(PLOT_BACKGROUND));
    dc.DrawRectangle(plot);

    // grid
    dc.SetTextForeground(AXIS_TEXT);
    for (int db = -60; db <= 0; db += 10) {
        const int y = static_cast<int>(to_y(db));
        dc.SetPen(wxPen(GRID, 1));
        dc.DrawLine(plot.GetLeft(), y, plot.GetRight(), y);
        draw_text_right(dc, wxString::Format("%d", db), plot.x - FromDIP(4), y);
    }
    // colour thresholds
    for (double db : {-5.0, -40.0}) {
        const int y = static_cast<int>(to_y(db));
        dc.SetPen(wxPen(energy_colour(db), 1, wxPENSTYLE_SHORT_DASH));
        dc.DrawLine(plot.GetLeft(), y, plot.GetRight(), y);
    }

    if (values.empty()) {
        dc.SetTextForeground(AXIS_TEXT);
        draw_text_centered(dc, "no signal", plot.x + plot.width / 2, plot.y + plot.height / 2 - FromDIP(6));
        return;
    }

    // newest sample on the right edge
    const double dx = static_cast<double>(plot.width) / (capacity - 1);
    auto to_x = [&](size_t i) { return plot.GetRight() - (values.size() - 1 - i) * dx; };

    for (size_t i = 1; i < values.size(); ++i) {
        const double mid = (values[i - 1] + values[i]) / 2.0;
        dc.SetPen(wxPen(energy_colour(mid), FromDIP(2)));
        dc.DrawLine(wxPoint(to_x(i - 1), to_y(values[i - 1])), wxPoint(to_x(i), to_y(values[i])));
    }
    const float last = values.back();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(energy_colour(last)));
    const int r = FromDIP(3);
    dc.DrawEllipse(static_cast<int>(to_x(values.size() - 1)) - r, static_cast<int>(to_y(last)) - r, 2 * r, 2 * r);

    wxFont bold = dc.GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    dc.SetFont(bold);
    dc.SetTextForeground(energy_colour(last));
    const wxString label = wxString::Format("%.1f dBFS", last);
    const wxSize e = dc.GetTextExtent(label);
    dc.DrawText(label, plot.GetRight() - e.x - FromDIP(6), plot.y + FromDIP(4));
}

// ---------------------------------------------------------------------------
// PassingChart
// ---------------------------------------------------------------------------

static constexpr double PASSING_MIN_DB = -48.0;
static constexpr double PASSING_MAX_DB = 2.0;

PassingChart::PassingChart(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(FromDIP(wxSize(300, 200)));
    Bind(wxEVT_PAINT, &PassingChart::on_paint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
}

void PassingChart::show(const PassingRecord& r) {
    record = r;
    Refresh();
}

void PassingChart::clear() {
    record.reset();
    Refresh();
}

void PassingChart::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC pdc(this);
    wxGCDC dc(pdc);
    const wxSize size = GetClientSize();

    dc.SetBackground(wxBrush(BACKGROUND));
    dc.Clear();
    const wxFont normal = GetFont();
    const wxFont small = small_font(this);

    const int left = FromDIP(52), right = FromDIP(16), top = FromDIP(34), bottom = FromDIP(38);
    const wxRect plot(left, top, size.x - left - right, size.y - top - bottom);
    if (plot.width < 40 || plot.height < 40) {
        return;
    }
    auto to_y = [&](double db) {
        db = std::clamp(db, PASSING_MIN_DB, PASSING_MAX_DB);
        return plot.y + (PASSING_MAX_DB - db) / (PASSING_MAX_DB - PASSING_MIN_DB) * plot.height;
    };

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(PLOT_BACKGROUND));
    dc.DrawRectangle(plot);

    // below the passing's noise floor: a second background colour
    if (record && record->noise_floor > PASSING_MIN_DB) {
        const int y = static_cast<int>(to_y(record->noise_floor));
        dc.SetBrush(wxBrush(BELOW_NOISE_BACKGROUND));
        dc.DrawRectangle(plot.x, y, plot.width, plot.GetBottom() - y + 1);
    }

    // y grid, fixed scale
    dc.SetFont(small);
    dc.SetTextForeground(AXIS_TEXT);
    for (int db = -48; db <= 0; db += 6) {
        const int y = static_cast<int>(to_y(db));
        dc.SetPen(wxPen(GRID, 1));
        dc.DrawLine(plot.GetLeft(), y, plot.GetRight(), y);
        draw_text_right(dc, wxString::Format("%d", db), plot.x - FromDIP(6), y);
    }
    dc.DrawRotatedText("RSSI (dBFS)", FromDIP(4), plot.y + plot.height / 2 + dc.GetTextExtent("RSSI (dBFS)").x / 2, 90);

    if (!record) {
        dc.SetFont(normal);
        dc.SetTextForeground(AXIS_TEXT);
        draw_text_centered(dc, "Select a passing below", plot.x + plot.width / 2, plot.y + plot.height / 2 - FromDIP(8));
        return;
    }
    const PassingRecord& rec = *record;

    // x range: every hit plus the helper lines, around the passing point (0 ms)
    std::vector<double> t_ms;
    t_ms.reserve(rec.hit_list.size());
    for (const auto& h : rec.hit_list) {
        t_ms.push_back((static_cast<int64_t>(h.timestamp) - static_cast<int64_t>(rec.timestamp_us)) / 1000.0);
    }
    const double half_ms = rec.duration_us / 2000.0;
    double xmin = -half_ms, xmax = half_ms;
    for (double t : t_ms) {
        xmin = std::min(xmin, t);
        xmax = std::max(xmax, t);
    }
    if (xmax - xmin < 2.0) {
        xmin -= 5.0;
        xmax += 5.0;
    }
    const double pad = (xmax - xmin) * 0.05;
    xmin -= pad;
    xmax += pad;
    auto to_x = [&](double t) { return plot.x + (t - xmin) / (xmax - xmin) * plot.width; };

    // x grid
    const double step = nice_step(xmax - xmin, std::max(2, plot.width / FromDIP(80)));
    for (double t = std::ceil(xmin / step) * step; t <= xmax; t += step) {
        const int x = static_cast<int>(to_x(t));
        dc.SetPen(wxPen(GRID, 1));
        dc.DrawLine(x, plot.GetTop(), x, plot.GetBottom());
        const double shown = std::fabs(t) < step * 1e-6 ? 0.0 : t;
        draw_text_centered(dc, wxString::Format("%g", shown), x, plot.GetBottom() + FromDIP(4));
    }
    draw_text_centered(dc, "time relative to passing point (ms)", plot.x + plot.width / 2, size.y - FromDIP(16));

    // passing point and +-duration/2 helpers
    if (rec.duration_us > 0) {
        dc.SetPen(wxPen(wxColour(110, 150, 255), FromDIP(1), wxPENSTYLE_SHORT_DASH));
        for (double t : {-half_ms, half_ms}) {
            const int x = static_cast<int>(to_x(t));
            dc.DrawLine(x, plot.GetTop(), x, plot.GetBottom());
        }
    }
    {
        const int x = static_cast<int>(to_x(0.0));
        dc.SetPen(wxPen(wxColour(255, 145, 0), FromDIP(2)));
        dc.DrawLine(x, plot.GetTop(), x, plot.GetBottom());
    }

    // noise floor label, right above the background split
    if (rec.noise_floor > PASSING_MIN_DB) {
        dc.SetTextForeground(NOISE_TEXT);
        const wxString s = wxString::Format("noise floor %.1f dBFS", rec.noise_floor);
        const wxSize e = dc.GetTextExtent(s);
        const int y = static_cast<int>(to_y(rec.noise_floor));
        dc.DrawText(s, plot.GetRight() - e.x - FromDIP(6), std::max(plot.y, y - e.y - FromDIP(2)));
    }

    // hits
    dc.SetPen(*wxTRANSPARENT_PEN);
    const int r = FromDIP(3);
    for (size_t i = 0; i < rec.hit_list.size(); ++i) {
        dc.SetBrush(wxBrush(mer_colour(rec.hit_list[i].mer)));
        dc.DrawEllipse(static_cast<int>(to_x(t_ms[i])) - r, static_cast<int>(to_y(rec.hit_list[i].rssi)) - r, 2 * r, 2 * r);
    }

    // header: what is shown
    dc.SetFont(normal);
    dc.SetTextForeground(TEXT);
    wxString header = wxString::Format("%s %u   %s   RSSI %.1f dBFS   %zu hits   MER %.1f dB",
        wxString::FromUTF8(rec.type), rec.transponder_id, rec.wall_time.Format("%H:%M:%S.%l"),
        rec.rssi, rec.hits, rec.mer);
    if (rec.duration_us > 0) {
        header += wxString::Format("   duration %.1f ms", rec.duration_us / 1000.0);
    }
    dc.DrawText(header, plot.x, FromDIP(8));

    // MER legend, top right
    dc.SetFont(small);
    int x = plot.GetRight();
    for (auto [mer, text] : {std::pair{9.0, "≥9 dB"}, std::pair{6.0, "6"}, std::pair{3.0, "MER ≤3"}}) {
        const wxString s = wxString::FromUTF8(text);
        const wxSize e = dc.GetTextExtent(s);
        x -= e.x;
        dc.SetTextForeground(AXIS_TEXT);
        dc.DrawText(s, x, FromDIP(12));
        x -= 2 * r + FromDIP(4);
        dc.SetBrush(wxBrush(mer_colour(mer)));
        dc.DrawEllipse(x, FromDIP(12) + e.y / 2 - r, 2 * r, 2 * r);
        x -= FromDIP(10);
    }
}
