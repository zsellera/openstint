# Starting the decoder on Windows

This project builds Windows binaries semi-regularly, and are available under the [latest-windows release page](https://github.com/zsellera/openstint/releases/tag/nightly-master). The provided `.zip` file contains the `openstint.exe` binary with all required dll-s and integrations.

1. download and extract the release
2. open a command line (press windows icon once, and type `cmd`), and `cd` into the extracted folder
3. type `openstint.exe` to start the binary

Alternatively, double-click to the exe, and start with defaults;
* ignore "untrusted publisher" warnings (the code is not signed, but automatically compiled -- you can verify the whole chain yourself)
* add to firewall except list (allowing connections from local/private network only)

## start.bat

We provide a `start.bat` with defaults you can override:

```
start "OpenStint" /HIGH /B openstint_rtlsdr.exe -g 20
```

* `start "OpenStint" /HIGH /B` - Windows starts OpenStint with *high priority*: this is important, timing precision depends on in
* `openstint_rtlsdr.exe` - for RTL-SDR; change to `openstint_hackrf.exe` if needed
* `-g 20`: set the radio's amplifier gain; for HackRF, there is an LNA (`-l 20`) and a VGA (`-v 20`) amplifier to play with.
