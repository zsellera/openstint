"""Minimal ctypes wrapper around librtlsdr for OpenStint Desktop.

Replaces pyrtlsdr to avoid calls (e.g. rtlsdr_set_dithering) that don't
exist in the MSYS2 MinGW build of librtlsdr.
"""

import ctypes
import ctypes.util
import os
import sys

# --- locate and load librtlsdr ------------------------------------------------

def _find_librtlsdr():
    if sys.platform == "win32":
        # PyInstaller bundle or same-directory DLL
        search_dirs = [
            getattr(sys, "_MEIPASS", None),
            os.path.dirname(os.path.abspath(__file__)),
            os.path.dirname(sys.executable),
        ]
        for d in search_dirs:
            if d is None:
                continue
            for name in ("rtlsdr.dll", "librtlsdr.dll"):
                path = os.path.join(d, name)
                if os.path.isfile(path):
                    return path
        return ctypes.util.find_library("rtlsdr") or "librtlsdr.dll"

    path = ctypes.util.find_library("rtlsdr")
    if path:
        return path
    for name in ("librtlsdr.so", "librtlsdr.dylib"):
        if os.path.isfile(f"/usr/local/lib/{name}"):
            return f"/usr/local/lib/{name}"
    return "librtlsdr"


_lib = ctypes.CDLL(_find_librtlsdr())

# --- types --------------------------------------------------------------------

_p_dev = ctypes.c_void_p  # rtlsdr_dev_t*

# void (*rtlsdr_read_async_cb_t)(unsigned char *buf, uint32_t len, void *ctx)
read_async_cb_t = ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_ubyte),
                                   ctypes.c_uint32, ctypes.c_void_p)

# enum rtlsdr_tuner
RTLSDR_TUNER_UNKNOWN = 0
RTLSDR_TUNER_E4000 = 1
RTLSDR_TUNER_FC0012 = 2
RTLSDR_TUNER_FC0013 = 3
RTLSDR_TUNER_FC2580 = 4
RTLSDR_TUNER_R820T = 5
RTLSDR_TUNER_R828D = 6

# --- function prototypes ------------------------------------------------------

_lib.rtlsdr_get_device_count.restype = ctypes.c_uint32
_lib.rtlsdr_get_device_count.argtypes = []

_lib.rtlsdr_get_device_name.restype = ctypes.c_char_p
_lib.rtlsdr_get_device_name.argtypes = [ctypes.c_uint32]

_lib.rtlsdr_get_index_by_serial.restype = ctypes.c_int
_lib.rtlsdr_get_index_by_serial.argtypes = [ctypes.c_char_p]

_lib.rtlsdr_open.restype = ctypes.c_int
_lib.rtlsdr_open.argtypes = [ctypes.POINTER(_p_dev), ctypes.c_uint32]

_lib.rtlsdr_close.restype = ctypes.c_int
_lib.rtlsdr_close.argtypes = [_p_dev]

_lib.rtlsdr_get_usb_strings.restype = ctypes.c_int
_lib.rtlsdr_get_usb_strings.argtypes = [_p_dev, ctypes.c_char_p,
                                         ctypes.c_char_p, ctypes.c_char_p]

_lib.rtlsdr_get_tuner_type.restype = ctypes.c_int
_lib.rtlsdr_get_tuner_type.argtypes = [_p_dev]

_lib.rtlsdr_set_center_freq.restype = ctypes.c_int
_lib.rtlsdr_set_center_freq.argtypes = [_p_dev, ctypes.c_uint32]

_lib.rtlsdr_set_sample_rate.restype = ctypes.c_int
_lib.rtlsdr_set_sample_rate.argtypes = [_p_dev, ctypes.c_uint32]

_lib.rtlsdr_set_tuner_bandwidth.restype = ctypes.c_int
_lib.rtlsdr_set_tuner_bandwidth.argtypes = [_p_dev, ctypes.c_uint32]

_lib.rtlsdr_set_tuner_gain_mode.restype = ctypes.c_int
_lib.rtlsdr_set_tuner_gain_mode.argtypes = [_p_dev, ctypes.c_int]

_lib.rtlsdr_set_tuner_gain.restype = ctypes.c_int
_lib.rtlsdr_set_tuner_gain.argtypes = [_p_dev, ctypes.c_int]

_lib.rtlsdr_get_tuner_gain.restype = ctypes.c_int
_lib.rtlsdr_get_tuner_gain.argtypes = [_p_dev]

_lib.rtlsdr_set_direct_sampling.restype = ctypes.c_int
_lib.rtlsdr_set_direct_sampling.argtypes = [_p_dev, ctypes.c_int]

_lib.rtlsdr_set_bias_tee.restype = ctypes.c_int
_lib.rtlsdr_set_bias_tee.argtypes = [_p_dev, ctypes.c_int]

_lib.rtlsdr_reset_buffer.restype = ctypes.c_int
_lib.rtlsdr_reset_buffer.argtypes = [_p_dev]

_lib.rtlsdr_read_async.restype = ctypes.c_int
_lib.rtlsdr_read_async.argtypes = [_p_dev, read_async_cb_t, ctypes.c_void_p,
                                    ctypes.c_uint32, ctypes.c_uint32]

_lib.rtlsdr_cancel_async.restype = ctypes.c_int
_lib.rtlsdr_cancel_async.argtypes = [_p_dev]

# --- public helpers -----------------------------------------------------------


def get_device_count():
    return _lib.rtlsdr_get_device_count()


def get_device_name(index):
    name = _lib.rtlsdr_get_device_name(index)
    return name.decode() if name else ""


# --- RtlSdr class -------------------------------------------------------------


class RtlSdr:
    """Lightweight wrapper matching the subset of pyrtlsdr used by app.py."""

    def __init__(self, device_index=0):
        self._dev = _p_dev()
        ret = _lib.rtlsdr_open(ctypes.byref(self._dev), int(device_index))
        if ret != 0:
            raise OSError(f"rtlsdr_open() failed: {ret}")
        self._async_cb_ref = None

    def close(self):
        if self._dev:
            _lib.rtlsdr_close(self._dev)
            self._dev = None

    # -- properties ------------------------------------------------------------

    @property
    def sample_rate(self):
        return _lib.rtlsdr_get_sample_rate(self._dev)

    @sample_rate.setter
    def sample_rate(self, rate):
        ret = _lib.rtlsdr_set_sample_rate(self._dev, int(rate))
        if ret != 0:
            raise OSError(f"rtlsdr_set_sample_rate() failed: {ret}")

    @property
    def center_freq(self):
        return _lib.rtlsdr_get_center_freq(self._dev)

    @center_freq.setter
    def center_freq(self, freq):
        ret = _lib.rtlsdr_set_center_freq(self._dev, int(freq))
        if ret != 0:
            raise OSError(f"rtlsdr_set_center_freq() failed: {ret}")

    @property
    def gain(self):
        return _lib.rtlsdr_get_tuner_gain(self._dev) / 10.0

    @gain.setter
    def gain(self, value_db):
        _lib.rtlsdr_set_tuner_gain_mode(self._dev, 1)
        ret = _lib.rtlsdr_set_tuner_gain(self._dev, int(value_db * 10))
        if ret != 0:
            raise OSError(f"rtlsdr_set_tuner_gain() failed: {ret}")

    # -- methods ---------------------------------------------------------------

    def set_bias_tee(self, enabled):
        _lib.rtlsdr_set_bias_tee(self._dev, 1 if enabled else 0)

    def set_bandwidth(self, bw_hz):
        _lib.rtlsdr_set_tuner_bandwidth(self._dev, int(bw_hz))

    def set_direct_sampling(self, mode):
        _lib.rtlsdr_set_direct_sampling(self._dev, int(mode))

    def get_tuner_type(self):
        return _lib.rtlsdr_get_tuner_type(self._dev)

    def get_usb_strings(self):
        m = ctypes.create_string_buffer(256)
        p = ctypes.create_string_buffer(256)
        s = ctypes.create_string_buffer(256)
        ret = _lib.rtlsdr_get_usb_strings(self._dev, m, p, s)
        if ret != 0:
            return None, None, None
        return m.value.decode(), p.value.decode(), s.value.decode()

    def read_bytes_async(self, callback, num_bytes):
        """Start async reading. Blocks until cancel_read_async() is called.

        callback(buf, context) -- buf is a ctypes c_ubyte array (supports
        the buffer protocol for pybind11 and numpy zero-copy access).
        """
        _lib.rtlsdr_reset_buffer(self._dev)

        def _c_callback(buf, length, _ctx):
            array = (ctypes.c_ubyte * length).from_address(
                ctypes.addressof(buf.contents))
            callback(array, None)

        self._async_cb_ref = read_async_cb_t(_c_callback)
        _lib.rtlsdr_read_async(self._dev, self._async_cb_ref, None, 0,
                               int(num_bytes))

    def cancel_read_async(self):
        if self._dev:
            _lib.rtlsdr_cancel_async(self._dev)
