# RC4 Transponder Learning

OpenStint can decode AMB RC4 (3-wire) and RC4 Hybrid (2-wire) transponders using a learning trick. Unlike OpenStint and RC3 transponders, RC4 protocol's decoding mechanism is not known to the public.

It is resolved with a **learning** approach: place the car on the antenna loop, let the decoder observe the RC4 payloads, and the system builds a lookup table that maps the observed payloads to a transponder ID. Once learned, the transponder is recognized on every subsequent passing.

### Step by step

1. **Place the car on the loop.** Park the car on the antenna loop and keep it still. The transponder should be within the detection area, simulating a car parked on the start/finish line.

2. **Wait for `START`.** The decoder monitors incoming RC4 frames. When it detects a stable signal (consistent RSSI, signal stronger than -20 dBFS), it enters learning mode:
   ```
   L 9042 START -16.2
   ```

3. **Keep the car stationary.** This is the critical part: do not move or reposition the transponder during training. If the car is moved or the signal becomes unstable, the decoder aborts:
   ```
   L 13984 INTERRUPTED
   ```
   If this happens, reposition the car and start over from step 1.

   The required training time depends on the transponder type:
   * **RC4 Hybrid**: approximately 25 seconds
   * **Pure RC4**: approximately 12 seconds (they transmit at a higher rate)

4. **Wait for `DONE`.** Once enough data has been collected, the decoder finalizes the learning and assigns a transponder ID:
   ```
   L 70192 DONE 1001 320
   ```
   This means transponder ID `1001` was learned with `320` distinct payloads. The car can now be removed from the loop.

### Transponder ID assignment

When learning completes, the decoder assigns an ID using the following logic:

* **RC4 Hybrid transponders** also transmit RC3 messages that contain a readable transponder ID. If the decoder detects such an ID during the training window, it uses it for the RC4 mapping as well. This means RC4 Hybrids are automatically assigned their real transponder ID.
* If the RC4 transponder was **already known** (previously learned), the existing ID is reused.
* **Pure RC4 transponders** have no readable ID. The decoder **auto-assigns** a numeric ID starting from 1000 and counting upward (`1000.rc4`, `1001.rc4`, etc.). You can rename these files to the actual transponder ID afterwards.

## The transponder database

Learned transponders are stored as plain-text files in the storage directory in `<transponder_id>.rc4` files.

The database is **hot-reloaded**: the decoder periodically scans the storage directory for new or removed `.rc4` files. This means you can add/delete/**rename** files.

This hot-reload also means you can prepare `.rc4` files externally (e.g. copy from another decoder or a shared database) and drop them into the storage directory at any time.

Note: modifying the *contents* of an already-loaded file requires a decoder restart, as files are not re-read once loaded.
