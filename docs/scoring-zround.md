# ZRound

[ZRound Suite](https://www.zround.com/index.php/sdm_downloads/zround-suite-latest/) is a very popular, free-to-use lap timing and race management software.

ZRound is not able to connect to OpenStint decoder, you have to use a *bridge*. The tool `bridge-zround.exe` creates such a bridge between ZRound and OpenStint, translating [OpenStint decoder protocol](decoder-protocol.md) to [ZRound protocol](https://www.zround.com/wiki/doku.php/lapcounters:protocols:open).

On windows, double-clicking on `bridge-zround.exe` opens it with default parameters, connecting to a local OpenStint decoder at `127.0.0.1:5556`, and offers ZRound protocol on port `5001`.

```
bridge-zround.exe --help
usage: bridge-zround.exe [-h] [--host HOST] [--port PORT] [--listen LISTEN]

Read OpenStint decoder and stream results to a ZRound

options:
  -h, --help       show this help message and exit
  --host HOST      OpenStint host
  --port PORT      OpenStint port
  --listen LISTEN  ZRound protocol listen port
```

## Connecting from ZRound

Use "ZRound" decoder protocol, and connect to `127.0.0.1` on port `5001` (default, just check the "connect" checkbox).
