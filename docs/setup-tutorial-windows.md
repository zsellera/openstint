# Starting the decoder on Windows

This project builds Windows binaries semi-regularly, and are available under the [latest-windows release page](https://github.com/zsellera/openstint/releases/tag/latest-windows). The provided `.zip` file contains the `openstint.exe` binary with all required dll-s and integrations.

1. download and extract the release
2. open a command line (press windows icon once, and type `cmd`), and `cd` into the extracted folder
3. type `openstint.exe` to start the binary

Alternatively, double-click to the exe and see what happens (might warn you about internet-downloaded binary).

## Starting the integrations

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
