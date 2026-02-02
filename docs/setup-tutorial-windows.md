# Starting the decoder on Windows

This project builds Windows binaries semi-regularly, and are available under the [latest-windows release page](https://github.com/zsellera/openstint/releases/tag/latest-windows). The provided `.zip` file contains the `openstint.exe` binary with all required dll-s and integrations.

1. download and extract the release
2. open a command line (press windows icon once, and type `cmd`), and `cd` into the extracted folder
3. type `openstint.exe` to start the binary

Alternatively, double-click to the exe, and start with defaults;
* ignore "untrusted publisher" warnings (the code is not signed, but automatically compiled -- you can verify the whole chain yourself)
* add to firewall except list (allowing connections from local/private network only)

## Pre-build integrations

### bridge-p3.exe

Translates OpenStint decoder messages to MyLaps P3 system. Use with LiveTime Scoring and ZRound.

```
bridge-p3.exe --help
usage: bridge-p3.exe [-h] [--host HOST] [--port PORT] [--listen LISTEN] [--id ID]

Read OpenStint decoder and stream results as an AMB/P3 decoder

options:
  -h, --help       show this help message and exit
  --host HOST      OpenStint host
  --port PORT      OpenStint port
  --listen LISTEN  P3 protocol listen port
  --id ID          Decoder ID (max 4 ascii characters)
```

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
