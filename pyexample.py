import signal
import sys
import threading
import time
import ctypes
import ctypes.util

from rtlsdr import RtlSdr
import openstint

def print_passing(passing):
    print(f"PASSING: id={passing.transponder_id} "
          f"system={passing.transponder_system} "
          f"rssi={passing.rssi:.1f} "
          f"hits={passing.hits} "
          f"duration={passing.duration}")

def print_timesync(timesync):
    print(f"TIMESYNC: id={timesync.transponder_id} "
          f"transponder_ts={timesync.transponder_timestamp}")

def print_status(report_ts, noise, dc_offset, frames_rx, frames_processed):
    print(f"STATUS: ts={report_ts} noise={noise:.2f} dc={dc_offset:.2f} rx={frames_rx} proc={frames_processed}")

def print_rc4_training_start(report_ts, rssi):
    print(f"RC4 START: ts={report_ts} rssi={rssi:.1f}")

def print_rc4_training_interrupted(report_ts):
    print(f"RC4 INTERRUPTED: ts={report_ts}")

def print_rc4_training_done(report_ts, transponder_id, payload_count):
    print(f"RC4 DONE: ts={report_ts} id={transponder_id} payloads={payload_count}")

def print_rc4_training_reset(report_ts):
    print(f"RC4 RESET: ts={report_ts}")

reporter = openstint.Reporter()
reporter.on_passing(print_passing)
reporter.on_timesync(print_timesync)
reporter.on_status(print_status)
reporter.on_rc4_training_start(print_rc4_training_start)
reporter.on_rc4_training_interrupted(print_rc4_training_interrupted)
reporter.on_rc4_training_done(print_rc4_training_done)
reporter.on_rc4_training_reset(print_rc4_training_reset)

registry = openstint.RC4FileBasedRegistry(".")
engine = openstint.Engine(reporter, registry)

sdr = RtlSdr()
sdr.sample_rate = 2.5e6
sdr.center_freq = 5e6
sdr.gain = 6

do_exit = threading.Event()

def callback(raw, _context):
    if do_exit.is_set():
        sdr.cancel_read_async()
        return
    engine.detect_frames_raw(raw)

def set_realtime_priority():
    try:
        if sys.platform == "win32":
            kernel32 = ctypes.windll.kernel32
            HIGH_PRIORITY_CLASS = 0x00000080
            THREAD_PRIORITY_HIGHEST = 2
            kernel32.SetPriorityClass(kernel32.GetCurrentProcess(), HIGH_PRIORITY_CLASS)
            kernel32.SetThreadPriority(kernel32.GetCurrentThread(), THREAD_PRIORITY_HIGHEST)
        else:
            libc = ctypes.CDLL(ctypes.util.find_library("c"))
            SCHED_RR = 2
            class sched_param(ctypes.Structure):
                _fields_ = [("sched_priority", ctypes.c_int)]
            param = sched_param(sched_priority=47)
            libc.pthread_setschedparam(libc.pthread_self(), SCHED_RR, ctypes.byref(param))
    except Exception:
        pass

def rx_thread():
    set_realtime_priority()
    sdr.read_bytes_async(callback)

signal.signal(signal.SIGINT, lambda *_: do_exit.set())
signal.signal(signal.SIGTERM, lambda *_: do_exit.set())

thread = threading.Thread(target=rx_thread, daemon=True)
thread.start()

print("Streaming... stop with Ctrl-C")
while not do_exit.is_set():
    time.sleep(0.1)
    engine.report_detections()

thread.join(timeout=2)
sdr.close()
