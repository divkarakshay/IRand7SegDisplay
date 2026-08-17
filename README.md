# A23 front-panel → ESP32 DevKit V1

Repurposing the front panel PCB (`A23-20-USB-SOP-V1`) of an old consumer device:
3-digit 7-segment display, 6 tactile keys (MENU/OK/CH+/CH−/VOL+/VOL−) and an
IR receiver — driven by an FD650B-S display/keypad-driver IC over a 2-wire bus.

The existing FD650B-S is kept; no component is replaced. The ESP32 is the new
bus master for the display+keys, and the IR receiver output goes to a free GPIO.

---

## 1. Wiring

```
 ESP32 DevKit V1                A23 front-panel PCB
 ┌───────────────────┐         5-pin connector (silkscreen: VDD IR GND DIO CLK)
 │ 3V3  ─────────────┼────────►│ VDD
 │ GND  ─────────────┼────────►│ GND
 │ GPIO27 ◄──────────┼────────►│ IR      (IR receiver demodulated output)
 │ GPIO21 ◄─────────►┼────────►│ DIO     (FD650B-S DAT, bidirectional)
 │ GPIO22 ───────────┼────────►│ CLK     (FD650B-S CLK)
 └───────────────────┘         └─────────┘
```

| PCB signal | ESP32 | Notes |
|---|---|---|
| `VDD` | **3V3** | FD650B runs 2.7–5.5 V. IR receiver modules (VS1838B/TSOP38238…) run ≈2.5–5.5 V. Powering the whole panel at 3.3 V avoids all 5 V↔3.3 V level-shifting. |
| `GND` | GND | Common ground. |
| `IR` | GPIO27 | Demodulated IR stream, decoded with `IRremoteESP8266`. |
| `DIO` | GPIO21 | Bidirectional data. Chip drives it open-drain with internal ~7 kΩ pull-up; the code switches GPIO21 between OUTPUT and INPUT. |
| `CLK` | GPIO22 | Clock out, push-pull, ~100 kHz. |

Do NOT power the panel from 5 V and connect DIO/IR straight to the ESP32 —
FD650B outputs (open-drain, pulled up to VDD) and IR OUT would be at ~5 V,
and ESP32 GPIOs are **not 5 V tolerant**. If you must run at 5 V: resistor
dividers on `IR` and `DIO` (e.g. 10 kΩ to GND + series 20 kΩ), keep `CLK`/`VDD`
as-is (3.3 V > 0.6 × 5 V = 3.0 V VIH, marginal but OK), and give the display
its own 5 V feed.

### Before first power-up (multimeter, unpowered)

1. Verify the connector orientation (triangle marks pin 1 / the `VDD` end).
2. Continuity: `DIO`→FD650B DAT, `CLK`→FD650B CLK, `GND`→FD650B GND,
   `VDD`→FD650B VDD, `IR`→IR receiver OUT pin.
3. Continuity VDD–GND → expect ~0 Ω through 100 nF + 220 µF (this is normal,
   that's the on-board decoupling `C2`/`EC1`).
4. The `R2 = 222` (2.2 kΩ) and `C2 = 104` (100 nF) are typical FD650B
   interface/supply parts; no action needed.

---

## 2. The FD650B-S protocol (verified against chipyuan datasheet rev C1)

Not standard I2C — a compatible 2-wire bus (CLK + open-drain DIO with internal
7 kΩ pull-ups, MSB-first, ACK after each byte):

| Command | Meaning |
|---|---|
| `0x48` + settings | Mode: `settings = brightness<<4 \| on`; brightness nibble `0x0` = 8/8 duty (brightest) … `0x7` = dimmer |
| `0x68` `0x6A` `0x6C` `0x6E` + byte | Display data for digit 1…4 (addresses step by 2; 3 used here) |
| `0x4F` → read 1 byte | Key code |

Key byte: `bit7=0`, `bit6=1` pressed, `bits5..3` = row (KIn), `bit2=1`,
`bits1..0` = column (DIGn). Idle = `0x2E`. After a press, wait ~6 scan frames
(~42 ms) for stable data. CLK rising edge latches input; chip outputs data on
CLK falling edge. Max 4 MHz, datasheet recommends ≤100 kHz — the driver uses
100 kHz.

Display bytes are **common-cathode, active-high segments**:
`bit0=A … bit6=G, bit7=DP` (`0x3F` = "0"). Digits stay correct under any
physical segment-wire permutation.

---

## 3. The 12 questions

1. **VDD voltage** — unknown without the original PSU; irrelevant for the
   repurpose since we choose it: feed the panel 3.3 V from the ESP32.
2. **Connector pin order** — confirmed by silkscreen `VDD IR GND DIO CLK`;
   triangle marks the `VDD`/pin-1 end.
3. **DIO/CLK go straight to the FD650B?** — almost certainly (single-layer
   board, no other logic); verify with continuity in §1.
4. **FD650B controllable from ESP32?** — Yes; it's a slave needing exactly
   this 2-wire bus (same setup as Linux openvfd TV-box drivers).
5. **Protocol/timing** — §2.
6. **Read the 6 buttons** — yes, through the key-scan matrix via `0x4F`;
   the physical row/column assignment of the 6 keys is unknown → run the
   calibration procedure (§5).
7. **Drive the 3-digit display** — yes, `0x68/0x6A/0x6C` + segment bytes.
8. **IR receiver** — 3-pin module (VCC/GND/OUT), OUT → GPIO27, decode with
   `IRremoteESP8266` (auto-detects NEC/Sony/RC-5/6…, 38 kHz modules).
9. **5-pin connector is the complete interface** — yes: VDD, GND, IR, DIO, CLK.
10. **USB section** — it's just a pass-through footprint (`USB`/`4PIN` markings)
    routed to the main board; no independent function. Ignore it.
11. **Wiring diagram** — §1.
12. **Code** — `src/main.cpp` (demo + calibration), `src/FD650B.{h,cpp}` driver.

---

## 4. Build & flash (PlatformIO)

```bash
pio run -t upload          # esp32dev board, arduino framework
pio device monitor         # 115200 baud
```

The code is plain Arduino C++ — the `.ino`-compatible files can be dropped
into the Arduino IDE as well (`main.cpp` → rename to `.ino`, keep
`FD650B.h`/`FD650B.cpp` next to it).

## 4b. IR record / replay (recordings live on the PC, in this project)

The ESP32 never stores codes. It prints every received code to serial:

```
IR: type=3 val=0x00CF8976 bits=32
```

and transmits on demand when it receives the command
`SEND type=3 val=0x00CF8976 bits=32` over serial. All recordings are kept in
`ir_recordings.json` in the project directory, managed by
`tools/ir_recorder.py`:

```bash
python3 tools/ir_recorder.py record --label vol_up   # capture codes (Ctrl-C stops)
python3 tools/ir_recorder.py list                    # show saved codes
python3 tools/ir_recorder.py replay vol_up           # send a saved code
python3 tools/ir_recorder.py replay NEC 0x00CF8976 32 # ...or raw type/value/bits
```

Replay needs an IR LED on the panel (see §6 wiring note): `GPIO26 → 1kΩ →
NPN base (BC547/2N2222)`, collector to IR LED (940 nm) cathode, LED anode to
3V3, emitter to GND. The panel itself has no IR LED — only the receiver.

## 5. Bring-up / calibration procedure

1. Wire per §1. **Do not plug the panel's original connector into anything.**
2. Flash and open the monitor. Expected:
   - `FD650B alive (ACK ok)` — display powers on (shows "ESP", then the
     `8.8.8.` self-test, then the counter).
   - If ACK fails: check DIO/CLK swap, VDD/GND swap, connector orientation.
3. **Key calibration (verified):** press each of the 6 buttons once. Every
   press prints `Key! raw=0xXX row=r col=c`. On this unit all six keys sit on
   column 3, rows 0–5:
   `MENU=0x47, OK=0x4F, CH−=0x57, CH+=0x5F, VOL−=0x67, VOL+=0x6F`.
   That mapping is already filled in `KEY_RAW[]` in `src/main.cpp`.
4. **IR test (verified):** point the original remote at the receiver; serial
   prints `IR: type=3 val=0xA1DE... bits=32` (NEC) and the display echoes the
   last 3 hex digits. If a remote is unavailable, no output is expected.
5. **Buttons demo (verified):** VOL+/VOL− count 0–999, CH+/CH− brightness,
   MENU toggles display, OK resets.
6. **Display format:** 3 digits with decimal points (max `8.8.8.`).
   `showNumber(value, dpIndex)` lights the DP of digit `dpIndex`
   (`FD650B.cpp`). Leading zeros are not blanked.

## 5b. Known gotchas

- `decode_results.value` is a **`uint64_t`** (`IRrecv.h`). In `Serial.printf`
  it is passed as a 64-bit vararg (Xtensa aligns it to an even register pair),
  so any trailing `%s`/`%08X` reads the wrong slot — NEC values are 32-bit,
  leaving 0 in the high word, and `%s` then does `strlen(0)` → LoadProhibited
  panic. Always cast: `(uint32_t)(ir.value & 0xFFFFFFFF)`.
- `typeToString()` returns a **`String`** (not `char*`) in this library
  version — never pass it to `%s` directly.

## 6. Files

| File | Purpose |
|---|---|
| `src/FD650B.h/.cpp` | Bit-banged FD650B-S driver (display + keys), self-contained |
| `src/main.cpp` | IR record/replay + buttons + display |
| `tools/ir_recorder.py` | PC-side store of recordings (`ir_recordings.json`), replay driver |
| `ir_recordings.json` | Recorded IR codes (created by `tools/ir_recorder.py`) |
| `platformio.ini` | esp32dev / arduino framework / IRremoteESP8266 dep |
