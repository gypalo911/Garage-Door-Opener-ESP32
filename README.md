# ESP32 HomeKit Garage Door Opener (GANT-GM 800/3000)

Control a GANT-GM 800/3000 garage/gate automation from Apple HomeKit (Home app + Siri)
using an ESP32-WROOM-32 and a CC1101 433.92 MHz transceiver.

It works the same way a BMW/Tesla HomeLink does for a **fixed-code** remote: it **captures**
your existing GANT remote's RF signal once ("learn"), stores it, and **replays** it whenever
you tap the garage tile in HomeKit.

> This project is for a garage/gate **you own or are explicitly authorized to operate**.
> See the Disclaimer at the bottom.

---

## How it works

```
Apple Home app / Siri ──HAP/WiFi──► ESP32 (HomeSpan) ──► RF backend ──► CC1101 ──)))──► GANT receiver
```

- HomeKit accessory type: **GarageDoorOpener** (open/close tile, open-loop state).
- RF: fixed-code **OOK** capture + replay at **433.92 MHz** via CC1101 in async mode.
- The captured pulse-train is stored in ESP32 NVS, so it survives power cycles.

Note: v1 is **open-loop** — there is no door position sensor, so after a command the tile
animates for `DOOR_TRAVEL_MS` (default 15 s) and then assumes the door reached the target
state. An optional reed switch can be added later for true position feedback.

---

## Hardware & wiring

- ESP32-WROOM-32 DevKit
- CC1101 433 MHz transceiver module (**3.3 V only — never 5 V**)
- Antenna wire (~17.3 cm quarter-wave for 433 MHz)

CC1101 ↔ ESP32 (VSPI):

| CC1101 pin | ESP32 GPIO | Function          |
|------------|-----------|-------------------|
| VCC        | 3V3       | Power (3.3 V)     |
| GND        | GND       | Ground            |
| SCK        | GPIO18    | SPI clock         |
| MISO (SO)  | GPIO19    | SPI MISO          |
| MOSI (SI)  | GPIO23    | SPI MOSI          |
| CSN        | GPIO5     | Chip select       |
| GDO0       | GPIO25    | Async OOK data (RX capture + TX) |
| GDO2       | GPIO4     | Required — used by setGDO() for TX |

Pins are defined in `include/config.h`.

> Both GDO0 and GDO2 must be wired: the transmit path calls
> `ELECHOUSE_cc1101.setGDO(gdo0, gdo2)`, which configures both pins. Leaving GDO2
> unconnected was one cause of transmit failing during bring-up.
>
> GDO0 is on GPIO25 (not GPIO2). GPIO2 is an ESP32 strapping pin: with the CC1101
> wired to it, the bootloader is blocked during flashing and the onboard-LED/
> pulldown fights the RX data line. GPIO25 avoids both problems.

---

## Build & flash (PlatformIO)

```bash
pio run                              # build
pio run --target upload              # flash (set upload_port in platformio.ini if needed)
```

The macOS serial port name for the CH340 adapter changes between reconnects
(e.g. `/dev/cu.usbserial-10`, `-110`). Update `upload_port`/`monitor_port` in
`platformio.ini` to match, or pass `--upload-port /dev/cu.usbserial-XX`.

---

## First-time setup

### 1. Provision WiFi (credentials are stored on-device, not in code)
Open the serial monitor at 115200 baud, type `W` + Enter, then follow the prompts to pick
your 2.4 GHz network and enter the password. (HomeKit requires 2.4 GHz; the ESP32 has no
5 GHz radio.)

### 2. Pair in the Home app
- Home app → **+** → **Add Accessory** → **More options…**
- Select **ESP32 Garage Door**
- Enter setup code **111-22-333**
- Accept the "Uncertified Accessory" warning → assign a room

### 3. Learn your GANT remote (requires CC1101 wired)
In the serial monitor:
- Type `L` + Enter → `LEARN MODE: press your GANT remote now (15 s)...`
- Press and hold your GANT remote button, close to the CC1101.
- On success: `Signal captured & saved!` followed by a dump of the pulse timings.

### 4. Test
- Type `T` + Enter to replay the stored signal (fires the door) without using HomeKit, or
- Tap the Garage Door tile in the Home app / "Hey Siri, open the garage door".

---

## Serial console commands
HomeSpan has built-in single-letter commands (e.g. `W`, `L`, `D`...). This
project's custom commands are invoked with the HomeSpan user-command **`@`
prefix** so they don't collide with the built-ins. Type the command + Enter:

| Cmd  | Action                                                     |
|------|------------------------------------------------------------|
| `W`  | WiFi setup (SSID + password) — HomeSpan built-in           |
| `@L` | Learn / capture the GANT remote RF signal                  |
| `@T` | Test-transmit the stored signal (fires the door)           |
| `@I` | Test-transmit with INVERTED carrier polarity (diagnostic)  |
| `@C` | Capture a fresh press and compare to stored (no overwrite) |
| `@D` | Dump stored RF signal timings                              |
| `@F` | Forget / erase the stored RF signal                        |
| `?`  | HomeSpan help (lists all commands)                         |

---

## Working CC1101 configuration (what makes TX actually open the door)

Getting the CC1101 to transmit a replayable OOK burst required this exact setup
(derived from the proven alx2k / simondankelmann projects). This is implemented
in `src/cc1101_fixedcode.cpp`:

Transmit path (`enterTxAsync`):
```cpp
ELECHOUSE_cc1101.Init();
ELECHOUSE_cc1101.setGDO(gdo0, gdo2);  // drives GDO0 as OUTPUT (ESP -> chip). KEY.
ELECHOUSE_cc1101.setMHZ(433.92);
ELECHOUSE_cc1101.SetTx();
ELECHOUSE_cc1101.setModulation(2);    // ASK/OOK
ELECHOUSE_cc1101.setDRate(512);       // data rate used by the working project
ELECHOUSE_cc1101.setPktFormat(3);     // asynchronous serial; data in on GDO0
// then bit-bang GDO0 HIGH/LOW per captured durations (HIGH = carrier on)
```

Receive/capture path (`enterRxAsync`): `setCCMode(0)` (async serial, GDO0 outputs
demodulated data) + `setModulation(2)` + `setRxBW(256)` + `SetRx()`, then an ISR
timestamps edges on GDO0.

Pitfalls that cost us during bring-up (documented so they don't bite again):
- Using `setGDO0()` alone left GDO0 as an INPUT, so the bit-banged TX data never
  reached the chip — transmit was silent on air. `setGDO(gdo0, gdo2)` fixes it.
- GDO2 must be physically wired (GPIO4); `setGDO` needs it.
- GDO0 on GPIO2 (strapping pin) blocked flashing and gave flaky capture — use GPIO25.
- Both polarities are available (`@T` normal, `@I` inverted) in case a remote's
  demodulated polarity is inverted; for this GANT remote, normal (`@T`) works.

---

## Troubleshooting

- **`CC1101 init: NOT DETECTED`** — module not wired or miswired. Check VCC=3V3 (not 5 V),
  and that CSN/SCK/MISO/MOSI aren't swapped. The firmware still runs HomeKit in simulation
  mode without the module.
- **Learn times out** — press the remote closer to the module, hold the button, ensure it's
  actually 433.92 MHz. Use `D` to inspect what (if anything) was captured.
- **Door doesn't respond to replay** — the remote may be rolling-code (not fixed-code). If
  so, replay cannot work; use the relay + spare-remote fallback described in
  `docs/03-implementation-plan.md` (Phase 2C).
- **Tile shows wrong state** — v1 is open-loop; tune `DOOR_TRAVEL_MS` in `config.h` to your
  door's real travel time, or add a reed switch (future enhancement).

---

## Project layout
```
include/config.h            Pins, frequency, HomeKit settings, door travel time
include/rf_backend.h        RF backend interface (swappable)
include/cc1101_fixedcode.h  Fixed-code capture/replay backend (declaration)
src/cc1101_fixedcode.cpp    ISR-based OOK capture + replay, NVS persistence
src/main.cpp                HomeSpan GarageDoorOpener + serial commands
docs/                       Technical analysis, spec, implementation plan
```

---

## Disclaimer

This firmware is intended solely for operating a garage/gate that **you own or are explicitly
authorized to use**. It captures and replays the signal of a fixed-code remote you physically
possess — the electronic equivalent of pressing your own remote. It does **not** defeat,
crack, or bypass rolling-code security, and it contains **no** jamming functionality. You are
responsible for complying with your local radio-frequency regulations and laws. The authors
accept no liability for misuse or for damage/injury — garage doors are heavy and can cause
harm; never operate the door without a clear line of sight to it.
