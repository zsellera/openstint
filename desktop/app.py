import sys
import os
import asyncio
import signal
import threading
import time
import ctypes
import ctypes.util
import tkinter as tk
from tkinter import ttk
from datetime import datetime

import zmq
import zmq.asyncio
import numpy as np
from PIL import Image, ImageTk
from rtlsdr_wrapper import RtlSdr, get_device_count, get_device_name

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'build', 'src'))
import openstint

SAMPLE_RATE = 2.5e6
CENTER_FREQ = 5e6
FFT_SIZE = 128
WATERFALL_LINES = 100
WATERFALL_WIDTH = FFT_SIZE


def set_realtime_priority():
    try:
        if sys.platform == "win32":
            kernel32 = ctypes.windll.kernel32
            kernel32.SetPriorityClass(kernel32.GetCurrentProcess(), 0x00000080)
            kernel32.SetThreadPriority(kernel32.GetCurrentThread(), 2)
        else:
            libc = ctypes.CDLL(ctypes.util.find_library("c"))
            SCHED_RR = 2

            class sched_param(ctypes.Structure):
                _fields_ = [("sched_priority", ctypes.c_int)]

            param = sched_param(sched_priority=47)
            libc.pthread_setschedparam(libc.pthread_self(), SCHED_RR, ctypes.byref(param))
    except Exception:
        pass


def enumerate_rtlsdr_devices():
    count = get_device_count()
    devices = []
    for i in range(count):
        name = get_device_name(i)
        devices.append((i, f"[{i}] {name}"))
    return devices


class SdrController:
    def __init__(self, app):
        self.app = app
        self.sdr = None
        self.thread = None
        self.running = False
        self.stop_event = threading.Event()
        self._last_waterfall_time = 0.0

    def open(self, device_index, gain):
        self.sdr = RtlSdr(device_index)
        self.sdr.sample_rate = SAMPLE_RATE
        self.sdr.center_freq = CENTER_FREQ
        self.sdr.gain = gain

    def start(self, device_index, gain):
        if self.running:
            return
        self.stop_event.clear()
        self.open(device_index, gain)
        self.running = True
        self.thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.thread.start()

    def stop(self):
        if not self.running:
            return
        self.stop_event.set()
        if self.sdr:
            self.sdr.cancel_read_async()
        if self.thread:
            self.thread.join(timeout=3)
        if self.sdr:
            self.sdr.close()
            self.sdr = None
        self.running = False

    def set_gain(self, gain):
        if self.sdr and self.running:
            self.sdr.gain = gain

    def set_bias_tee(self, enabled):
        if self.sdr and self.running:
            self.sdr.set_bias_tee(enabled)

    def _rx_loop(self):
        set_realtime_priority()

        def callback(raw, _context):
            if self.stop_event.is_set():
                self.sdr.cancel_read_async()
                return
            self.app.engine.detect_frames_raw(raw)
            self._compute_waterfall(raw)

        self.sdr.read_bytes_async(callback, 32768)

    def _compute_waterfall(self, raw):
        now = time.monotonic()
        if now - self._last_waterfall_time < 0.1:
            return
        self._last_waterfall_time = now

        iq = np.frombuffer(raw, dtype=np.uint8).astype(np.float32)
        n_iq = len(iq) // 2
        iq = iq[:n_iq * 2].reshape(n_iq, 2)
        x = (iq[:, 0] - 128.0) + 1j * (iq[:, 1] - 128.0)

        num_rows = len(x) // FFT_SIZE
        if num_rows == 0:
            return

        segments = x[:num_rows * FFT_SIZE].reshape(num_rows, FFT_SIZE)
        spectra = np.fft.fftshift(np.abs(np.fft.fft(segments, axis=1)), axes=1)
        spectrogram = 10 * np.log10(np.maximum(spectra ** 2, 1e-20))

        self.app.waterfall_queue.append(spectrogram.max(axis=0))


class ZRoundBridge:
    LISTEN_PORT = 5001

    def __init__(self, zmq_port, log_fn):
        self._zmq_url = f"tcp://127.0.0.1:{zmq_port}"
        self._log = log_fn
        self._loop = None
        self._thread = None
        self._server = None
        self._writer = None
        self._race_running = False
        self._base_decoder_ts = None
        self._base_race_time = 0.0

    def start(self):
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        if self._loop and self._loop.is_running():
            self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread:
            self._thread.join(timeout=3)
        self._writer = None
        self._race_running = False
        self._base_decoder_ts = None
        self._thread = None
        self._loop = None

    def _run(self):
        asyncio.set_event_loop(self._loop)
        try:
            self._loop.run_until_complete(self._serve())
        except Exception:
            pass

    async def _serve(self):
        self._server = await asyncio.start_server(
            self._handle_zround, "0.0.0.0", self.LISTEN_PORT)
        self._log(f"[zround] listening on port {self.LISTEN_PORT}")

        ctx = zmq.asyncio.Context()
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt_string(zmq.SUBSCRIBE, "")
        sock.connect(self._zmq_url)
        self._log(f"[zround] subscribed to {self._zmq_url}")

        async with self._server:
            await asyncio.gather(
                self._read_openstint(sock),
                self._server.serve_forever(),
            )

    async def _handle_zround(self, reader, writer):
        self._writer = writer
        peer = writer.get_extra_info("peername")
        self._log(f"[zround] connected from {peer}")
        try:
            while True:
                data = await reader.readuntil(b"&")
                msg = data.decode().strip()
                self._log(f"[zround] RX {msg}")

                if msg.startswith("%C"):
                    await self._send("%A&")
                elif msg.startswith("%I"):
                    self._race_running = True
                    self._base_decoder_ts = None
                    self._base_race_time = time.monotonic()
                    self._log("[zround] race started")
                elif msg.startswith("%F"):
                    self._race_running = False
                    self._log("[zround] race stopped")
        except asyncio.IncompleteReadError:
            pass
        except Exception as e:
            self._log(f"[zround] connection error: {e}")
        finally:
            self._log("[zround] disconnected")
            self._writer = None

    async def _send(self, msg):
        if self._writer:
            self._writer.write(msg.encode())
            await self._writer.drain()
            self._log(f"[zround] TX {msg.strip()}")

    async def _read_openstint(self, sock):
        while True:
            msg = await sock.recv_string()
            parts = msg.split()
            if parts[0] == "P" and len(parts) >= 5:
                await self._on_passing(int(parts[1]), int(parts[3]))
            elif parts[0] == "S" and len(parts) >= 6:
                await self._on_status(int(parts[1]))

    async def _on_passing(self, decoder_time, transponder_id):
        if not self._race_running:
            return
        decoder_time_seconds = decoder_time / 1000.0
        if self._base_decoder_ts is None:
            time_passed = time.monotonic() - self._base_race_time
            self._base_decoder_ts = decoder_time_seconds - time_passed
            self._log(f"[zround] set base_decoder_ts={self._base_decoder_ts:.3f}")
        race_time = int(1000 * (decoder_time_seconds - self._base_decoder_ts))
        await self._send(f"%L{transponder_id:X},{race_time:X}&")

    async def _on_status(self, decoder_time):
        if not self._race_running or self._base_decoder_ts is None:
            return
        decoder_time_seconds = decoder_time / 1000.0
        race_time = int(1000 * (decoder_time_seconds - self._base_decoder_ts))
        await self._send(f"%T{race_time:X}&")


class OpenStintDesktop:
    ZMQ_PORT = 5556

    def __init__(self):
        self.zmq_context = zmq.Context()
        self.zmq_publisher = self.zmq_context.socket(zmq.PUB)
        try:
            self.zmq_publisher.bind(f"tcp://*:{self.ZMQ_PORT}")
        except zmq.ZMQError as e:
            root = tk.Tk()
            root.withdraw()
            from tkinter import messagebox
            messagebox.showerror(
                "OpenStint Desktop",
                f"Failed to bind ZMQ publisher on port {self.ZMQ_PORT}:\n{e}")
            root.destroy()
            sys.exit(1)

        self.root = tk.Tk()
        self.root.title("OpenStint Desktop")
        self.root.geometry("1100x700")
        self.root.minsize(900, 500)

        self.reporter = openstint.Reporter()
        self.registry = openstint.RC4FileBasedRegistry(".")
        self.engine = openstint.Engine(self.reporter, self.registry)

        self.sdr_controller = SdrController(self)
        self.zround_bridge = ZRoundBridge(self.ZMQ_PORT, self._log_event)
        self.waterfall_queue = []
        self.waterfall_data = np.full((WATERFALL_LINES, WATERFALL_WIDTH), -60.0)

        self.stats_noise = 0.0
        self.stats_frames_rx = 0
        self.stats_frames_decoded = 0
        self.stats_passings = 0

        self._setup_callbacks()
        self._build_ui()
        self._poll_loop()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _setup_callbacks(self):
        self.reporter.on_passing(self._on_passing)
        self.reporter.on_timesync(self._on_timesync)
        self.reporter.on_status(self._on_status)
        self.reporter.on_rc4_training_start(self._on_rc4_start)
        self.reporter.on_rc4_training_interrupted(self._on_rc4_interrupted)
        self.reporter.on_rc4_training_done(self._on_rc4_done)
        self.reporter.on_rc4_training_reset(self._on_rc4_reset)

        self._pending_events = []
        self._pending_passings = []
        self._pending_stats = []
        self._lock = threading.Lock()

    def _log_event(self, message):
        with self._lock:
            self._pending_events.append(message)

    _SYSTEM_NAMES = {
        openstint.TransponderSystem.OpenStint: "OPN",
        openstint.TransponderSystem.AMB: "AMB",
    }

    def _zmq_send(self, message):
        self.zmq_publisher.send_string(message)
        with self._lock:
            self._pending_events.append(message)

    def _on_passing(self, passing):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        line = (f"{ts}  ID={passing.transponder_id}  "
                f"sys={passing.transponder_system}  "
                f"RSSI={passing.rssi:.1f}  "
                f"hits={passing.hits}  "
                f"dur={passing.duration}")
        sys_name = self._SYSTEM_NAMES.get(passing.transponder_system, "OPN")
        self._zmq_send(
            f"P {int(passing.timestamp/1000)} {sys_name} "
            f"{passing.transponder_id} {passing.rssi:.2f} "
            f"{passing.hits} {passing.duration}")
        detections = [(d.timecode, d.rssi) for d in passing.detections]
        passing_ts = passing.timestamp
        first_det_ts = passing.detections[0].timestamp if passing.detections else 0
        duration_us = passing.duration
        with self._lock:
            self._pending_passings.append((line, detections, passing_ts, first_det_ts, duration_us))

    def _on_timesync(self, ts):
        sys_name = self._SYSTEM_NAMES.get(ts.transponder_system, "OPN")
        self._zmq_send(
            f"T {int(ts.timestamp/1000)} {sys_name} "
            f"{ts.transponder_id} {ts.transponder_timestamp}")

    def _on_status(self, report_ts, noise, dc_offset, frames_rx, frames_processed):
        self._zmq_send(
            f"S {report_ts} {noise:.2f} {dc_offset:.2f} "
            f"{frames_rx} {frames_processed}")
        with self._lock:
            self._pending_stats.append((noise, frames_rx, frames_processed))

    def _on_rc4_start(self, report_ts, rssi):
        self._zmq_send(f"L {report_ts} START {rssi:.1f}")

    def _on_rc4_interrupted(self, report_ts):
        self._zmq_send(f"L {report_ts} INTERRUPTED")

    def _on_rc4_done(self, report_ts, transponder_id, payload_count):
        self._zmq_send(f"L {report_ts} DONE {transponder_id} {payload_count}")

    def _on_rc4_reset(self, report_ts):
        self._zmq_send(f"L {report_ts} RESET")

    def _build_ui(self):
        style = ttk.Style()
        style.theme_use("clam")

        outer = ttk.Frame(self.root)
        outer.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        sidebar = ttk.Frame(outer, width=260)
        sidebar.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 4))
        sidebar.pack_propagate(False)

        content = ttk.Frame(outer)
        content.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self._build_sidebar(sidebar)
        self._build_content(content)

    def _build_sidebar(self, parent):
        # --- Radio Parameters ---
        radio_frame = ttk.LabelFrame(parent, text="Radio", padding=6)
        radio_frame.pack(fill=tk.X, padx=4, pady=(4, 2))

        device_row = ttk.Frame(radio_frame)
        device_row.pack(fill=tk.X, pady=2)

        self.device_var = tk.StringVar()
        self.device_combo = ttk.Combobox(
            device_row, textvariable=self.device_var,
            state="readonly", width=20)
        self.device_combo.pack(side=tk.LEFT, fill=tk.X, expand=True)

        refresh_btn = ttk.Button(device_row, text="↻", width=3,
                                 command=self._refresh_devices)
        refresh_btn.pack(side=tk.RIGHT, padx=(4, 0))

        self._device_list = []
        self._refresh_devices()

        # Gain
        gain_frame = ttk.Frame(radio_frame)
        gain_frame.pack(fill=tk.X, pady=2)
        ttk.Label(gain_frame, text="Gain:").pack(side=tk.LEFT)
        self.gain_label = ttk.Label(gain_frame, text="20.0 dB", width=8)
        self.gain_label.pack(side=tk.RIGHT)

        self.gain_var = tk.DoubleVar(value=20.0)
        self.gain_slider = ttk.Scale(
            radio_frame, from_=0, to=50, variable=self.gain_var,
            orient=tk.HORIZONTAL, command=self._on_gain_change)
        self.gain_slider.pack(fill=tk.X, pady=2)

        # Bias-tee
        self.bias_tee_var = tk.BooleanVar(value=False)
        bias_check = ttk.Checkbutton(
            radio_frame, text="Bias-Tee", variable=self.bias_tee_var,
            command=self._on_bias_tee_toggle)
        bias_check.pack(anchor=tk.W, pady=2)

        # Start/Stop
        self.start_stop_var = tk.StringVar(value="▶  Start")
        self.start_stop_btn = ttk.Button(
            radio_frame, textvariable=self.start_stop_var,
            command=self._on_start_stop)
        self.start_stop_btn.pack(fill=tk.X, pady=(4, 0))

        # --- Statistics ---
        stats_frame = ttk.LabelFrame(parent, text="Statistics", padding=6)
        stats_frame.pack(fill=tk.X, padx=4, pady=4)

        self.stat_labels = {}
        for label_text, key in [("Noise:", "noise"),
                                ("Frames RX:", "frames_rx"),
                                ("Frames Decoded:", "frames_decoded"),
                                ("Passings:", "passings")]:
            row = ttk.Frame(stats_frame)
            row.pack(fill=tk.X, pady=1)
            ttk.Label(row, text=label_text).pack(side=tk.LEFT)
            lbl = ttk.Label(row, text="0", anchor=tk.E)
            lbl.pack(side=tk.RIGHT)
            self.stat_labels[key] = lbl

        # --- ZRound Bridge ---
        zround_frame = ttk.LabelFrame(parent, text="ZRound Bridge", padding=6)
        zround_frame.pack(fill=tk.X, padx=4, pady=4)

        zround_row = ttk.Frame(zround_frame)
        zround_row.pack(fill=tk.X)

        self.zround_var = tk.BooleanVar(value=False)
        zround_toggle = ttk.Checkbutton(
            zround_row, text="Enable", variable=self.zround_var,
            command=self._on_zround_toggle)
        zround_toggle.pack(side=tk.LEFT)

        self.zround_status_label = ttk.Label(
            zround_row, text="Disabled", foreground="gray")
        self.zround_status_label.pack(side=tk.RIGHT)

        # --- Waterfall ---
        wf_frame = ttk.LabelFrame(parent, text="Waterfall", padding=4)
        wf_frame.pack(fill=tk.BOTH, expand=True, padx=4, pady=(2, 4))

        self.waterfall_canvas = tk.Canvas(wf_frame, bg="black", height=150)
        self.waterfall_canvas.pack(fill=tk.BOTH, expand=True)
        self.waterfall_photo = None

    def _build_content(self, parent):
        content_pane = ttk.PanedWindow(parent, orient=tk.VERTICAL)
        content_pane.pack(fill=tk.BOTH, expand=True)

        # Detection graph (top 1/3)
        graph_frame = ttk.LabelFrame(parent, text="Detection Graph", padding=4)
        content_pane.add(graph_frame, weight=1)

        self.detection_canvas = tk.Canvas(
            graph_frame, bg="#1e1e1e", height=150)
        self.detection_canvas.pack(fill=tk.BOTH, expand=True)
        self._passing_detections = {}

        # Passings list (middle 1/3)
        passings_frame = ttk.LabelFrame(parent, text="Recent Passings", padding=4)
        content_pane.add(passings_frame, weight=1)

        columns = ("time", "id", "system", "rssi", "hits", "duration")
        self.passings_tree = ttk.Treeview(
            passings_frame, columns=columns, show="headings", height=12)
        for col, heading, w in [
            ("time", "Time", 100), ("id", "ID", 80),
            ("system", "System", 70), ("rssi", "RSSI", 60),
            ("hits", "Hits", 50), ("duration", "Duration", 70)
        ]:
            self.passings_tree.heading(col, text=heading)
            self.passings_tree.column(col, width=w, minwidth=40)

        scroll_p = ttk.Scrollbar(passings_frame, orient=tk.VERTICAL,
                                 command=self.passings_tree.yview)
        self.passings_tree.configure(yscrollcommand=scroll_p.set)
        self.passings_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll_p.pack(side=tk.RIGHT, fill=tk.Y)

        self.passings_tree.bind("<<TreeviewSelect>>", self._on_passing_selected)

        # Event log (bottom 1/3)
        log_frame = ttk.LabelFrame(parent, text="Event Log", padding=4)
        content_pane.add(log_frame, weight=1)

        self.log_text = tk.Text(log_frame, height=8, state=tk.DISABLED,
                                font=("Courier", 10), bg="#1e1e1e", fg="#cccccc",
                                wrap=tk.WORD)
        scroll_l = ttk.Scrollbar(log_frame, orient=tk.VERTICAL,
                                 command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll_l.set)
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll_l.pack(side=tk.RIGHT, fill=tk.Y)

    def _refresh_devices(self):
        self._device_list = enumerate_rtlsdr_devices()
        names = [d[1] for d in self._device_list]
        self.device_combo["values"] = names
        if names:
            self.device_combo.current(0)

    def _on_gain_change(self, _value):
        gain = self.gain_var.get()
        self.gain_label.config(text=f"{gain:.1f} dB")
        self.sdr_controller.set_gain(gain)

    def _on_bias_tee_toggle(self):
        self.sdr_controller.set_bias_tee(self.bias_tee_var.get())

    def _on_zround_toggle(self):
        if self.zround_var.get():
            try:
                self.zround_bridge.start()
                self.zround_status_label.config(
                    text=f"Listening on port {ZRoundBridge.LISTEN_PORT}",
                    foreground="green")
            except Exception as e:
                self.zround_var.set(False)
                self.zround_status_label.config(
                    text="Failed to start", foreground="red")
                self._append_log(f"[zround] ERROR: {e}")
        else:
            self.zround_bridge.stop()
            self.zround_status_label.config(
                text="Disabled", foreground="gray")

    def _on_start_stop(self):
        if self.sdr_controller.running:
            self.sdr_controller.stop()
            self.start_stop_var.set("▶  Start")
        else:
            if not self._device_list:
                return
            idx = self.device_combo.current()
            if idx < 0:
                return
            device_index = self._device_list[idx][0]
            gain = self.gain_var.get()
            try:
                self.sdr_controller.start(device_index, gain)
                if self.bias_tee_var.get():
                    self.sdr_controller.set_bias_tee(True)
                self.start_stop_var.set("■  Stop")
            except Exception as e:
                self._append_log(f"ERROR: {e}")

    def _poll_loop(self):
        if self.sdr_controller.running:
            self.engine.report_detections()

        with self._lock:
            events = self._pending_events[:]
            self._pending_events.clear()
            passings = self._pending_passings[:]
            self._pending_passings.clear()
            stats = self._pending_stats[:]
            self._pending_stats.clear()

        for line in events:
            self._append_log(line)

        for line, detections, passing_ts, first_det_ts, duration_us in passings:
            parts = line.split("  ")
            vals = {}
            for p in parts:
                p = p.strip()
                if "=" in p:
                    k, v = p.split("=", 1)
                    vals[k] = v
                elif not vals:
                    vals["time"] = p
            iid = self.passings_tree.insert("", 0, values=(
                vals.get("time", ""),
                vals.get("ID", ""),
                vals.get("sys", ""),
                vals.get("RSSI", ""),
                vals.get("hits", ""),
                vals.get("dur", ""),
            ))
            self._passing_detections[iid] = (detections, passing_ts, first_det_ts, duration_us)
            self.stats_passings += 1
            self.passings_tree.selection_set(iid)

        for noise, frames_rx, frames_decoded in stats:
            self.stats_noise = noise
            self.stats_frames_rx += frames_rx
            self.stats_frames_decoded += frames_decoded

        self.stat_labels["noise"].config(text=f"{self.stats_noise:.2f}")
        self.stat_labels["frames_rx"].config(text=str(self.stats_frames_rx))
        self.stat_labels["frames_decoded"].config(text=str(self.stats_frames_decoded))
        self.stat_labels["passings"].config(text=str(self.stats_passings))

        self._update_waterfall()

        self.root.after(100, self._poll_loop)

    def _append_log(self, text):
        self.log_text.config(state=tk.NORMAL)
        self.log_text.insert(tk.END, text + "\n")
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)

    def _update_waterfall(self):
        if not self.waterfall_queue:
            return

        lines = self.waterfall_queue[:]
        self.waterfall_queue.clear()

        for line in lines:
            self.waterfall_data = np.roll(self.waterfall_data, -1, axis=0)
            self.waterfall_data[-1, :] = line

        canvas_w = self.waterfall_canvas.winfo_width()
        canvas_h = self.waterfall_canvas.winfo_height()
        if canvas_w < 2 or canvas_h < 2:
            return

        data = self.waterfall_data
        normalized = np.clip((data / 128.0), 0, 1)

        # black -> blue -> cyan -> yellow -> white
        r = np.zeros_like(normalized, dtype=np.uint8)
        g = np.zeros_like(normalized, dtype=np.uint8)
        b = np.zeros_like(normalized, dtype=np.uint8)

        # 0.00-0.25: black to blue
        m = normalized < 0.25
        t = normalized[m] / 0.25
        b[m] = (t * 255).astype(np.uint8)

        # 0.25-0.50: blue to cyan
        m = (normalized >= 0.25) & (normalized < 0.5)
        t = (normalized[m] - 0.25) / 0.25
        b[m] = 255
        g[m] = (t * 255).astype(np.uint8)

        # 0.50-0.75: cyan to yellow
        m = (normalized >= 0.5) & (normalized < 0.75)
        t = (normalized[m] - 0.5) / 0.25
        r[m] = (t * 255).astype(np.uint8)
        g[m] = 255
        b[m] = ((1 - t) * 255).astype(np.uint8)

        # 0.75-1.00: yellow to white
        m = normalized >= 0.75
        t = (normalized[m] - 0.75) / 0.25
        r[m] = 255
        g[m] = 255
        b[m] = (t * 255).astype(np.uint8)

        pixels = np.stack([r, g, b], axis=-1)

        img = Image.fromarray(pixels, 'RGB')
        img = img.resize((canvas_w, canvas_h), Image.NEAREST)
        self.waterfall_photo = ImageTk.PhotoImage(img)
        self.waterfall_canvas.create_image(0, 0, anchor=tk.NW,
                                           image=self.waterfall_photo)

    def _on_passing_selected(self, _event):
        selection = self.passings_tree.selection()
        if not selection:
            return
        iid = selection[0]
        data = self._passing_detections.get(iid)
        if not data:
            self.detection_canvas.delete("all")
            return
        detections, passing_ts, first_det_ts, duration_us = data
        self._draw_detection_graph(detections, passing_ts, first_det_ts, duration_us)

    def _draw_detection_graph(self, detections, passing_ts, first_det_ts, duration_us):
        self.detection_canvas.delete("all")
        if not detections:
            return

        cw = self.detection_canvas.winfo_width()
        ch = self.detection_canvas.winfo_height()
        if cw < 20 or ch < 20:
            return

        margin_l, margin_r, margin_t, margin_b = 50, 20, 10, 30
        plot_w = cw - margin_l - margin_r
        plot_h = ch - margin_t - margin_b
        if plot_w < 10 or plot_h < 10:
            return

        tc0 = detections[0][0]
        times_ms = [(tc - tc0) / (SAMPLE_RATE / 1000.0) for tc, _ in detections]
        rssis = [rssi for _, rssi in detections]

        t_min, t_max = 0.0, max(times_ms) if max(times_ms) > 0 else 1.0
        r_min = min(-43.0, min(rssis) - 1.0)
        r_max = max(0.0, max(rssis) + 1.0)
        if r_max - r_min < 2.0:
            r_min -= 1.0
            r_max += 1.0

        def to_x(t_val):
            return margin_l + (t_val - t_min) / (t_max - t_min) * plot_w

        def to_y(rssi_val):
            return margin_t + (1.0 - (rssi_val - r_min) / (r_max - r_min)) * plot_h

        # axes
        self.detection_canvas.create_line(
            margin_l, margin_t, margin_l, ch - margin_b, fill="#555555")
        self.detection_canvas.create_line(
            margin_l, ch - margin_b, cw - margin_r, ch - margin_b, fill="#555555")

        # RSSI axis labels (vertical, left side)
        n_rticks = 4
        for i in range(n_rticks + 1):
            r_val = r_min + (r_max - r_min) * i / n_rticks
            y = to_y(r_val)
            self.detection_canvas.create_line(
                margin_l - 3, y, margin_l, y, fill="#555555")
            self.detection_canvas.create_text(
                margin_l - 5, y, anchor=tk.E, fill="#aaaaaa",
                text=f"{r_val:.0f}", font=("Courier", 8))

        # RSSI axis title
        self.detection_canvas.create_text(
            12, (margin_t + ch - margin_b) / 2, anchor=tk.W,
            fill="#aaaaaa", text="RSSI", font=("Courier", 8), angle=90)

        # Time axis labels (horizontal, bottom)
        n_ticks = 5
        for i in range(n_ticks + 1):
            t_val = t_min + (t_max - t_min) * i / n_ticks
            x = to_x(t_val)
            self.detection_canvas.create_line(
                x, ch - margin_b, x, ch - margin_b + 3, fill="#555555")
            self.detection_canvas.create_text(
                x, ch - margin_b + 5, anchor=tk.N, fill="#aaaaaa",
                text=f"{t_val:.0f}", font=("Courier", 8))

        # Time axis title
        self.detection_canvas.create_text(
            (margin_l + cw - margin_r) / 2, ch - 3, anchor=tk.S,
            fill="#aaaaaa", text="ms", font=("Courier", 8))

        # Passing vertical line
        # passing_ts = first_det.timestamp + timecode_to_usec(weighted_tc)
        # offset in usec = passing_ts - first_det_ts
        # offset in ms = offset_usec / 1000
        passing_ms = (passing_ts - first_det_ts) / 1000.0
        if t_min <= passing_ms <= t_max:
            px = to_x(passing_ms)
            self.detection_canvas.create_line(
                px, margin_t, px, ch - margin_b, fill="#ff6600", dash=(4, 2))

        if duration_us > 0:
            half_dur_ms = duration_us / 2000.0
            for edge_ms in (passing_ms - half_dur_ms, passing_ms + half_dur_ms):
                if t_min <= edge_ms <= t_max:
                    ex = to_x(edge_ms)
                    self.detection_canvas.create_line(
                        ex, margin_t, ex, ch - margin_b,
                        fill="#0011ff", dash=(2, 4))

        # detection dots
        for t_ms, rssi in zip(times_ms, rssis):
            x = to_x(t_ms)
            y = to_y(rssi)
            self.detection_canvas.create_oval(
                x - 3, y - 3, x + 3, y + 3, fill="#00ccff", outline="")

    def _on_close(self):
        self.zround_bridge.stop()
        self.sdr_controller.stop()
        self.zmq_publisher.close()
        self.zmq_context.term()
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    app = OpenStintDesktop()
    app.run()
