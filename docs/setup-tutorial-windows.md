# Starting the decoder on Windows

This project builds Windows binaries semi-regularly, and are available under the [latest-windows release page](https://github.com/zsellera/openstint/releases/tag/nightly-master). The provided `.zip` file contains the `openstint.exe` binary with all required dll-s and integrations.

1. download and extract the release
2. open a command line (press windows icon once, and type `cmd`), and `cd` into the extracted folder
3. type `openstint.exe` to start the binary

Alternatively, double-click to the exe, and start with defaults;
* ignore "untrusted publisher" warnings (the code is not signed, but automatically compiled -- you can verify the whole chain yourself)
* add to firewall except list (allowing connections from local/private network only)

## Pre-build integrations

### bridge-zround.exe

```
bridge-zround.py.exe --help
usage: bridge-zround.exe [-h] [--host HOST] [--port PORT] [--listen LISTEN]

Read OpenStint decoder and stream results to a ZRound

options:
  -h, --help       show this help message and exit
  --host HOST      OpenStint host
  --port PORT      OpenStint port
  --listen LISTEN  ZRound protocol listen port
```

### bridge-rcgtiming.exe

RCGTiming is an online application. The "bridge-rcgtiming" streams the decoder's output to their backend. On their web interface, register a decoder, and click "Driver App" download. Download only the "Configuration update"; the details on the download screen won't matter later. There is a human-readable "main.conf" in the zip file, and an authentication token inside. The bridge-rcgtiming is asking for it (enter without the quotation marks).

## Starting other integrations

This project comes with its own communiaction protocol. To interface with existing laptimer applications, we have to run integrations. To run them, install python from [python.org](https://www.python.org/downloads/).

Then, in a cmd window, install ptoject dependencies:

```cmd
python -m pip install tornado pyzmq
```

To run an integration:

```cmd
cd Downloads\openstint-windows-x64
python integrations\bridge-zround.py
```

## Connecting ZRound

[ZRound Suite](https://www.zround.com/index.php/sdm_downloads/zround-suite-latest/)

Use "ZRound" decoder protocol, and connect to `127.0.0.1` on port `5001` (default, just check the "connect" checkbox).
