// OpenStint desktop: RTL-SDR decoder with a wxWidgets front-end.
//
// The decoder core (src/commons.cpp) is the same one the openstint_rtlsdr CLI
// runs, ZeroMQ publishing included, so scoring integrations keep working. The
// GUI additionally receives every passing in-process (set_decoder_listener()).

#include <chrono>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/stdpaths.h>
#include <wx/taskbar.h>

#include "app_icon.hpp"

#include "charts.hpp"
#include "commons.hpp"
#include "passing.hpp"
#include "radio.hpp"

static const size_t MAX_PASSINGS = 1000;
static const int DEFAULT_GAIN_DB = 20;
static const int MAX_GAIN_DB = 50;
static const int64_t RX_TIMEOUT_MS = 2000;
static const float INITIAL_NOISE_FLOOR_DB = -100.0f; // until the first status message
static const size_t NOISE_HISTORY = 32;              // status messages kept, ~1 s apart

// ---------------------------------------------------------------------------
// passing table: a virtual list over the frame's deque (newest first)
// ---------------------------------------------------------------------------

class PassingList : public wxListCtrl {
public:
    PassingList(wxWindow* parent, const std::deque<PassingRecord>& records)
        : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES),
          records(records) {
        AppendColumn("Time", wxLIST_FORMAT_LEFT, FromDIP(110));
        AppendColumn("Type", wxLIST_FORMAT_LEFT, FromDIP(60));
        AppendColumn("Transponder", wxLIST_FORMAT_RIGHT, FromDIP(100));
        AppendColumn("RSSI (dBFS)", wxLIST_FORMAT_RIGHT, FromDIP(90));
        AppendColumn("Hits", wxLIST_FORMAT_RIGHT, FromDIP(60));
        AppendColumn("MER (dB)", wxLIST_FORMAT_RIGHT, FromDIP(80));
    }

protected:
    wxString OnGetItemText(long item, long column) const override {
        if (item < 0 || static_cast<size_t>(item) >= records.size()) {
            return {};
        }
        const PassingRecord& r = records[item];
        switch (column) {
            case 0: return r.wall_time.Format("%H:%M:%S.%l");
            case 1: return wxString::FromUTF8(r.type);
            case 2: return wxString::Format("%u", r.transponder_id);
            case 3: return wxString::Format("%.1f", r.rssi);
            case 4: return wxString::Format("%zu", r.hits);
            case 5: return wxString::Format("%.1f", r.mer);
        }
        return {};
    }

private:
    const std::deque<PassingRecord>& records;
};

// ---------------------------------------------------------------------------
// main window
// ---------------------------------------------------------------------------

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    wxWindow* build_controls(wxWindow* parent);
    void refresh_devices();
    void update_controls();
    void start_radio();
    void stop_radio(const wxString& reason = {});
    void on_timer(wxTimerEvent&);
    void on_passing(const Passing& passing);
    void on_status(const StatusReport& status);
    void reset_status();
    void on_rc4_learning(const Rc4LearningReport& report);
    float noise_floor_at(uint64_t timestamp_us) const;
    void on_selection(wxListEvent& e);
    void update_status_bar();

    RtlSdrRadio radio;
    std::vector<RtlSdrDeviceInfo> devices;
    std::deque<PassingRecord> passings; // newest first
    size_t passing_count = 0;
    bool selecting = false; // programmatic selection in progress
    float noise_floor = INITIAL_NOISE_FLOOR_DB;
    std::deque<std::pair<uint64_t, float>> noise_history; // (steady_now_us() at receipt, noise floor dBFS)

    wxChoice* device_choice = nullptr;
    wxButton* refresh_button = nullptr;
    wxButton* start_button = nullptr;
    wxSlider* gain_slider = nullptr;
    wxStaticText* gain_label = nullptr;
    wxCheckBox* bias_tee_check = nullptr;
    wxStaticText* state_label = nullptr;
    wxStaticText* noise_value = nullptr;
    wxStaticText* jitter_value = nullptr;
    StatusPill* rc4_indicator = nullptr;
    EnergyChart* energy_chart = nullptr;
    PassingChart* passing_chart = nullptr;
    PassingList* passing_list = nullptr;
    wxTimer timer;
};

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "OpenStint", wxDefaultPosition, wxSize(1200, 760)) {
    SetMinSize(FromDIP(wxSize(900, 560)));
    SetIcons(app_icon_bundle()); // title bar / taskbar (Windows, Linux)

    auto* split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_3DSASH);
    wxWindow* controls = build_controls(split);

    auto* right_split = new wxSplitterWindow(split, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                             wxSP_LIVE_UPDATE | wxSP_3DSASH);
    passing_chart = new PassingChart(right_split);
    passing_list = new PassingList(right_split, passings);
    right_split->SetMinimumPaneSize(FromDIP(120));
    right_split->SetSashGravity(0.5);                                 // resizing keeps it 50-50
    right_split->SplitHorizontally(passing_chart, passing_list, 0);   // 0: start in the middle

    split->SetMinimumPaneSize(FromDIP(240));
    split->SetSashGravity(0.0);
    split->SplitVertically(controls, right_split, FromDIP(300));

    CreateStatusBar();

    passing_list->Bind(wxEVT_LIST_ITEM_SELECTED, &MainFrame::on_selection, this);
    timer.Bind(wxEVT_TIMER, &MainFrame::on_timer, this);
    timer.Start(100);

    // report_detections() runs on the GUI thread (on_timer), so does the listener
    set_decoder_listener({
        .on_passing = [this](const Passing& p) { on_passing(p); },
        .on_status = [this](const StatusReport& s) { on_status(s); },
        .on_rc4_learning = [this](const Rc4LearningReport& r) { on_rc4_learning(r); },
    });

    refresh_devices();
    reset_status();
    update_status_bar();
}

MainFrame::~MainFrame() {
    timer.Stop();
    set_decoder_listener({});
    radio.stop();
}

wxWindow* MainFrame::build_controls(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // --- radio ---
    auto* radio_box = new wxStaticBoxSizer(wxVERTICAL, panel, "RTL-SDR");
    wxWindow* rb = radio_box->GetStaticBox();

    auto* device_row = new wxBoxSizer(wxHORIZONTAL);
    device_choice = new wxChoice(rb, wxID_ANY);
    refresh_button = new wxButton(rb, wxID_ANY, "Refresh list");
    device_row->Add(device_choice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    device_row->Add(refresh_button, 0, wxALIGN_CENTER_VERTICAL);
    radio_box->Add(device_row, 0, wxEXPAND | wxALL, FromDIP(4));

    start_button = new wxButton(rb, wxID_ANY, "Start");
    radio_box->Add(start_button, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

    gain_label = new wxStaticText(rb, wxID_ANY, "Gain");
    radio_box->Add(gain_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
    gain_slider = new wxSlider(rb, wxID_ANY, DEFAULT_GAIN_DB, 0, MAX_GAIN_DB);
    radio_box->Add(gain_slider, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

    bias_tee_check = new wxCheckBox(rb, wxID_ANY, "Bias-tee (+4.5 V)");
    radio_box->Add(bias_tee_check, 0, wxALL, FromDIP(4));

    state_label = new wxStaticText(rb, wxID_ANY, "Stopped");
    radio_box->Add(state_label, 0, wxEXPAND | wxALL, FromDIP(4));

    sizer->Add(radio_box, 0, wxEXPAND | wxALL, FromDIP(6));

    // --- decoder status ("S" messages), two readouts side by side ---
    auto* status_box = new wxStaticBoxSizer(wxVERTICAL, panel, "Status");
    wxWindow* sb = status_box->GetStaticBox();
    auto* readouts = new wxGridSizer(2, 2, FromDIP(2), FromDIP(12)); // 2 rows: caption, value
    auto* noise_caption = new wxStaticText(sb, wxID_ANY, "Noise floor");
    auto* jitter_caption = new wxStaticText(sb, wxID_ANY, "Jitter");
    noise_value = new wxStaticText(sb, wxID_ANY, wxEmptyString);
    jitter_value = new wxStaticText(sb, wxID_ANY, wxEmptyString);
    for (auto* caption : {noise_caption, jitter_caption}) {
        caption->SetFont(caption->GetFont().Smaller());
        caption->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    }
    for (auto* value : {noise_value, jitter_value}) {
        value->SetFont(value->GetFont().Larger().Bold());
    }
    readouts->Add(noise_caption, 0, wxEXPAND);
    readouts->Add(jitter_caption, 0, wxEXPAND);
    readouts->Add(noise_value, 0, wxEXPAND);
    readouts->Add(jitter_value, 0, wxEXPAND);
    status_box->Add(readouts, 0, wxEXPAND | wxALL, FromDIP(4));
    rc4_indicator = new StatusPill(sb);
    status_box->Add(rc4_indicator, 0, wxEXPAND | wxALL, FromDIP(4));
    sizer->Add(status_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

    // --- signal ---
    auto* signal_box = new wxStaticBoxSizer(wxVERTICAL, panel, "Peak signal energy");
    energy_chart = new EnergyChart(signal_box->GetStaticBox());
    signal_box->Add(energy_chart, 1, wxEXPAND | wxALL, FromDIP(4));
    sizer->Add(signal_box, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

    panel->SetSizer(sizer);

    refresh_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_devices(); });
    device_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { update_controls(); });
    start_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (radio.running()) {
            stop_radio();
        } else {
            start_radio();
        }
    });
    gain_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (radio.running()) {
            radio.set_gain(gain_slider->GetValue() * 10);
        }
        update_controls();
    });
    bias_tee_check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        radio.set_bias_tee(bias_tee_check->GetValue());
    });

    return panel;
}

void MainFrame::refresh_devices() {
    devices = enumerate_rtlsdr_devices();
    device_choice->Clear();
    for (const auto& d : devices) {
        device_choice->Append(wxString::FromUTF8(d.label));
    }
    if (devices.empty()) {
        device_choice->Append("No RTL-SDR found");
        device_choice->SetSelection(0);
        device_choice->Disable();
    } else {
        device_choice->SetSelection(0);
        device_choice->Enable();
    }
    update_controls();
}

void MainFrame::update_controls() {
    const bool running = radio.running();
    const int sel = device_choice->GetSelection();
    const bool has_device = !devices.empty() && sel >= 0 && static_cast<size_t>(sel) < devices.size();

    // before opening, the USB product string is the best guess; once running, the tuner type decides
    const bool direct = running ? radio.direct_sampling() : (has_device && !devices[sel].likely_v4);

    device_choice->Enable(!running && !devices.empty());
    refresh_button->Enable(!running);
    start_button->SetLabel(running ? "Stop" : "Start");
    start_button->Enable(running || has_device);

    gain_slider->Enable(!direct && (running || has_device));
    if (direct) {
        gain_label->SetLabel("Gain: n/a (direct sampling)");
    } else {
        gain_label->SetLabel(wxString::Format("Gain: %d dB", gain_slider->GetValue()));
    }

    if (running) {
        state_label->SetLabel("Running, " + wxString::FromUTF8(radio.description()));
    }
    state_label->GetContainingSizer()->Layout();
}

void MainFrame::start_radio() {
    const int sel = device_choice->GetSelection();
    if (devices.empty() || sel < 0) {
        return;
    }
    std::string error;
    if (!radio.start(devices[sel].index, gain_slider->GetValue() * 10, bias_tee_check->GetValue(), error)) {
        state_label->SetLabel("Failed to start");
        update_controls();
        wxMessageBox(wxString::FromUTF8(error), "OpenStint", wxOK | wxICON_ERROR, this);
        return;
    }
    energy_chart->clear();
    reset_status();
    update_controls();
    update_status_bar();
}

void MainFrame::stop_radio(const wxString& reason) {
    radio.stop();
    // flush transponders still being tracked: pretend the reporting deadline has passed
    report_detections(steady_now_us() + 1000000ull);
    state_label->SetLabel(reason.empty() ? wxString("Stopped") : reason);
    update_controls();
    update_status_bar();
}

void MainFrame::on_timer(wxTimerEvent&) {
    if (!radio.running()) {
        return;
    }
    if (!radio.streaming() || radio.ms_since_last_rx() > RX_TIMEOUT_MS) {
        stop_radio("Stopped: no samples, device lost?");
        return;
    }
    report_detections(steady_now_us()); // calls on_passing()
    for (float e : radio.take_energy()) {
        energy_chart->push(e);
    }
}

void MainFrame::on_passing(const Passing& p) {
    using namespace std::chrono;
    // decoder timestamps are steady-clock microseconds; anchor them to the wall clock now
    const int64_t age_us = static_cast<int64_t>(steady_now_us()) - static_cast<int64_t>(p.timestamp);
    const int64_t wall_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - age_us / 1000;

    PassingRecord rec{
        wxDateTime(wxLongLong(wall_ms)),
        transponder_system_name(p.transponder_type),
        p.transponder_id,
        p.rssi,
        p.hits,
        p.snr,
        p.duration,
        p.timestamp,
        noise_floor_at(p.hit_list.empty() ? p.timestamp : p.hit_list.front().timestamp),
        p.hit_list,
    };

    // keep following the newest passing unless the user picked an older one
    const long selected = passing_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    const bool follow = selected <= 0;

    passings.push_front(std::move(rec));
    if (passings.size() > MAX_PASSINGS) {
        passings.pop_back();
    }
    ++passing_count;

    selecting = true;
    if (selected >= 0) {
        passing_list->SetItemState(selected, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
    passing_list->SetItemCount(passings.size());
    const long new_selection = follow ? 0 : selected + 1; // older rows moved down by one
    if (static_cast<size_t>(new_selection) < passings.size()) {
        const long mask = wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED;
        passing_list->SetItemState(new_selection, mask, mask);
    }
    selecting = false;

    if (follow) {
        passing_chart->show(passings.front());
        passing_list->EnsureVisible(0);
    }
    passing_list->Refresh();
    update_status_bar();
}

void MainFrame::on_status(const StatusReport& status) {
    // the noise floor is -infinity until the decoder had a frame-free buffer to
    // estimate it on; keep the previous value until then
    if (std::isfinite(status.noise_floor)) {
        noise_floor = status.noise_floor;
        noise_history.emplace_back(steady_now_us(), noise_floor);
        while (noise_history.size() > NOISE_HISTORY) {
            noise_history.pop_front();
        }
    }
    noise_value->SetLabel(wxString::Format("%.1f dBFS", noise_floor));
    jitter_value->SetLabel(wxString::Format("%.2f ms", status.jitter_p95_ms));
}

// called on startup and on every radio start
void MainFrame::reset_status() {
    noise_floor = INITIAL_NOISE_FLOOR_DB;
    noise_history.clear();
    noise_value->SetLabel(wxString::Format("%.1f dBFS", noise_floor));
    jitter_value->SetLabel(wxString::FromUTF8("–"));
    rc4_indicator->set("RC4 learning: idle", wxColour(120, 120, 128));
}

// The trainer's events map to the indicator states one to one. INTERRUPTED is
// shown until the next session starts; RESET (the learned car left the loop)
// returns to idle. Stopping the radio flushes the trainer, which reports either.
void MainFrame::on_rc4_learning(const Rc4LearningReport& r) {
    using Event = Rc4LearningReport::Event;
    switch (r.event) {
        case Event::Start:
            rc4_indicator->set("RC4 learning: started", wxColour(40, 110, 220),
                               wxString::Format("Learning a transponder at %.1f dBFS", r.rssi));
            break;
        case Event::Interrupted:
            rc4_indicator->set("RC4 learning: interrupted", wxColour(225, 125, 20));
            break;
        case Event::Done:
            rc4_indicator->set(wxString::Format("RC4 learning: completed, %u", r.transponder_id),
                               wxColour(40, 160, 80),
                               wxString::Format("Transponder %u learned, %zu payloads stored",
                                                r.transponder_id, r.payload_count));
            break;
        case Event::Reset:
            rc4_indicator->set("RC4 learning: idle", wxColour(120, 120, 128));
            break;
    }
}

// the noise floor in effect at timestamp_us: the last one received before it
float MainFrame::noise_floor_at(uint64_t timestamp_us) const {
    for (auto it = noise_history.rbegin(); it != noise_history.rend(); ++it) {
        if (it->first <= timestamp_us) {
            return it->second;
        }
    }
    return INITIAL_NOISE_FLOOR_DB;
}

void MainFrame::on_selection(wxListEvent& e) {
    if (selecting) {
        return;
    }
    const long idx = e.GetIndex();
    if (idx >= 0 && static_cast<size_t>(idx) < passings.size()) {
        passing_chart->show(passings[idx]);
    }
}

void MainFrame::update_status_bar() {
    SetStatusText(wxString::Format("%s   |   %zu passings (%zu listed)",
        radio.running() ? "Receiving" : "Idle", passing_count, passings.size()));
}

// ---------------------------------------------------------------------------
// app
// ---------------------------------------------------------------------------

class OpenStintApp : public wxApp {
public:
    bool OnInit() override;
    int OnExit() override;

private:
    wxTaskBarIcon* dock_icon = nullptr; // macOS: sets the Dock icon of this (bundle-less) binary
};

int OpenStintApp::OnExit() {
    delete dock_icon;
    return wxApp::OnExit();
}

bool OpenStintApp::OnInit() {
    SetAppName("OpenStint");

    // the CLI's common options (-p port, -s dir, -t, -P ms, -R dB, -m) work here too.
    // RC4 registry: default to a per-user directory, the working directory of a
    // GUI app is often not writable
    std::vector<std::string> args{"openstint_desktop"};
    const wxString data_dir = wxStandardPaths::Get().GetUserDataDir();
    wxFileName::Mkdir(data_dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    args.push_back("-s");
    args.push_back(data_dir.utf8_string());
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i].utf8_string());
    }
    std::vector<char*> cargs;
    for (auto& a : args) {
        cargs.push_back(a.data());
    }
    const int cargc = static_cast<int>(cargs.size());
    for (int i = 1; i < cargc; ++i) {
        const std::string arg = cargs[i];
        if (!parse_common_arguments(i, cargc, arg, cargs.data())) {
            std::fprintf(stderr, "Ignoring unknown argument: %s\n", arg.c_str());
        }
    }

    try {
        init_commons();
    } catch (const std::exception& e) {
        wxMessageBox(wxString::Format("Failed to start the decoder:\n%s\n\nIs another OpenStint instance "
                                      "running on the same ZeroMQ port?", e.what()),
                     "OpenStint", wxOK | wxICON_ERROR);
        return false;
    }

#ifdef __WXOSX__
    dock_icon = new wxTaskBarIcon(wxTBI_DOCK);
    dock_icon->SetIcon(wxBitmapBundle::FromBitmaps(render_app_icon(256), render_app_icon(512)), "OpenStint");
#endif

    auto* frame = new MainFrame();
    frame->Show();
    return true;
}

wxIMPLEMENT_APP(OpenStintApp);
