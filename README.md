# IR Forward — DSTV remote control in another room

Use your DSTV remote in a bedroom while the decoder is in another room. A
small board in the bedroom picks up the remote's infrared signal and sends it
over WiFi (ESP-NOW, board to board) to a board at the decoder, which sends it
out again on an infrared LED in front of the decoder.

Extras on the receiver: record **IR sequences** from any remote (TV, sound
bar, DSTV), add known codes (e.g. Samsung TV Power ON / OFF), and run them on a
**weekly schedule** — for example a daily "reset" of TV, decoder and sound bar.

| Folder | Firmware | Where the board goes |
|---|---|---|
| `TV_IR_Sending` | **Sender (TX)** — one per room with a remote | in the room, IR receiver facing the remote |
| `TV_IR_Receiving` | **Receiver (RX)** — one per decoder | at the decoder, IR LED pointed at it |
| `TV_IR_IN_ONE` | **All-in-One** — sender and receiver on one board | when the remote's room and the decoder are next to each other (IR LED wired through) |

All three run on the same PCB (`PCB/IR_FORWARD_V1`), with a WEMOS LOLIN S2 Mini
(ESP32-S2). Printable enclosure parts (.stl: case, lid, IR clip, LED holders)
are in `Enclosure/`; the PCB (schematic, board, Gerber zip) is in `PCB/`.

## Hardware

- WEMOS LOLIN S2 Mini (ESP32-S2)
- TSOP31238 IR receiver (IR IN) — sender / All-in-One; optional on the receiver
- IR LED(s) via a 2N2222 driver (IR OUT) — receiver / All-in-One
- Red and green LEDs, buzzer, 230 V supply with surge protection monitor
- Full pin map: comment block at the top of each project's `src/main.cpp`

## Build and first flash

1. Install [PlatformIO](https://platformio.org) (VS Code extension or CLI).
2. Open the project folder of the firmware you want (e.g. `TV_IR_Receiving`).
3. Connect the S2 Mini by USB, hold button **0**, tap **RST** (download mode).
4. Flash over USB:
   - Sender: `pio run -e usb -t upload`
   - Receiver: `pio run -e rx_usb -t upload`
   - All-in-One: `pio run -e usb -t upload`
5. Press **RST**. (The upload may report "failed" only because the S2 cannot
   be reset over USB — the firmware is written.)

## First setup (once per board)

A new board has no WiFi settings. It opens its own **setup WiFi**:
`IR-FORWARD-TX-xxxx`, `IR-FORWARD-RX-xxxx` or `IR-FORWARD-IN1-xxxx` (open, no
password). The red and green LEDs blink alternately.

1. Join that WiFi with a phone. The setup page opens (or go to `http://192.168.4.1`).
2. Pick your home WiFi, enter its password.
3. IP address: tick **automatic (DHCP)**, or enter a fixed IP.
4. **Channel lock** (sender / receiver):
   - one router / access point: leave **Any channel**
   - several access points on different channels: set the **same** channel on
     every board — see *Several access points* below
5. **Choose an owner password** (6+ characters) and keep it safe. You need it
   to change the WiFi settings later and for every firmware update.
6. Save. The board restarts and joins your WiFi.

The setup WiFi comes back by itself if the board cannot connect 3 times in a
row (and leaves again after 5 minutes to retry), or when you hold button **0**
for 5 seconds (WiFi settings deleted). Saving there always needs the owner
password, so a neighbour joining the setup WiFi cannot change anything.

## Using it

Open the board's IP (or `http://<host name>.local`) in a browser.

- **Sender**: status, link to the receiver, settings, and **IR SEQUENCES** —
  record a sequence by pointing a remote at this board.
- **Receiver**: status of all senders, **IR SEQUENCES** (run, edit, add known
  codes such as Samsung TV Power ON/OFF) and **SCHEDULE** (run sequences at set
  times; per line: on/off, × repeats, every … ms, pause after).
- **WIFI** page on every board, and `/wifiscan`: which access points the
  board hears, on which channel and how strong.

## Firmware updates over WiFi

The owner password protects network updates. Set it once on your PC as the
environment variable `IR_OTA_PASS`, e.g. on Windows:

```
setx IR_OTA_PASS your-owner-password
```

(open a new terminal afterwards), then:

```
pio run -e ota -t upload --upload-port <board ip>        (sender, All-in-One)
pio run -e rx_ota -t upload --upload-port <board ip>     (receiver)
```

Own upload targets: copy `private.example.ini` to `private.ini` (not committed)
and add `[env:...]` sections with your boards' IPs.

All-in-One without PlatformIO: build, then upload
`firmware_out/TV_IR_IN_ONE_v<version>.bin` on `http://<board ip>/update`.

Forgot the owner password? Erase the board over USB
(`pio run -e usb -t erase`, receiver: `-e rx_usb`) and flash again; then do
the first setup again.

## Several access points

ESP-NOW only works between boards on the **same WiFi channel**. With several
access points on different channels:

1. Fix the 2.4 GHz channel of the access points you want to use (not *Auto*).
2. On each board, open `/wifiscan` and pick a channel every board hears well
   (better than about -70 dBm).
3. Set that **Channel lock** on every board's WIFI page.
4. If your WiFi system can lock clients to access points (e.g. TP-Link Omada
   "Lock to wireless device"), allow only the access points on that channel —
   **after** step 3, otherwise the boards lose WiFi until they restart.

## Optional: `src/Private.h`

Building several boards for yourself? Copy `src/Private.example.h` to
`src/Private.h` (never committed) to pre-load the owner password, your access
point names and (All-in-One) your WiFi networks into every board you flash.

## Licence

Copyright (c) 2026 Bennie Vermeulen. Licensed under
[CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/): you may use,
share and change it for **non-commercial** purposes, with credit to the author.
Selling the firmware, boards, enclosures or devices built from them is not
allowed. See `LICENSE`.

**Safety:** the board runs from 230 V mains. Building and installing it is at
your own risk — have mains wiring done or checked by a qualified person.
