---
title: "TrackTiming: Offline Lap Timing Web App"
description: TrackTiming is a mobile-friendly, offline lap timing and training web app for Raspberry Pi and OpenStint, with voice announcements and data export.
---

# TrackTiming

[TrackTiming](https://github.com/CyberChacal/TrackTiming) is a mobile-friendly web interface to track laps and training sessions using Raspberry + OpenStint. It includes simple functions such as voice announcements, taking notes and exporting data. All you need is a smartphone or a laptop with WiFi.

<img width="499" height="319" alt="image" src="https://github.com/user-attachments/assets/17b49888-8c98-416d-b746-d8068a565033" />

It is meant to be offline only, and accessed with a WiFi access point on the raspberry Pi. It can be interesting for people not having proper internet connection at the track. [Installation](https://github.com/CyberChacal/TrackTiming/blob/main/README_Install.txt) and [usage](https://github.com/CyberChacal/TrackTiming/blob/main/README_Use.txt) is described in the repository.

To test it locally,

```bash
# clone:
git clone https://github.com/CyberChacal/TrackTiming.git
cd TrackTiming/files

# create virtualenv:
python3 -m venv .venv
. .venv/bin/activate

# install dependencies
pip install flask zmq smbus2 gunicorn RPLCD

# hooray
python3 track_timing.py
```
