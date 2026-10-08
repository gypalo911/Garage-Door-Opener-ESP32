# Technical Analysis — ESP32 + CC1101 HomeKit Garage Door Opener

## 1. Goal

Build a device that opens/closes a garage (gate) door on command from Apple HomeKit,
analogous to how a Tesla HomeLink or BMW built-in opener works. The ESP32 exposes a
HomeKit accessory (as we already did for the Blue Light with HomeSpan); when toggled,
it transmits the correct sub-GHz RF signal via a CC1101 module to the garage's receiver.

This is the same conceptual pattern as our existing light project:
`HomeKit command -> ESP32 -> actuator`. Here the "actuator" is an RF transmitter
instead of a GPIO pin driving an LED.

## 2. How car-integrated openers (HomeLink / BMW) actually work

HomeLink and BMW/Audi built-in openers are **not** passive clone-and-replay devices.
They operate in one of two ways:

1. **Learn an existing fixed-code remote** by capturing its signal and replaying it
   verbatim (works only for fixed-code systems), OR
2. **Enroll as a NEW authorized remote** into the garage receiver via the receiver's
   "learn"/"program" button — the same procedure you'd use to add a factory spare
   remote. For rolling-code systems this is the ONLY legitimate path.

Our device must follow the same model. We are building a *legitimate additional remote*
for a door **you own and control**, not a tool to defeat someone else's security.

## 3. The decisive technical fork: fixed-code vs rolling-code

This single property of YOUR garage remote determines the entire design and whether
replay is even possible.

### 3a. Fixed-code (static) systems — e.g. PT2262 / EV1527 / older CAME / Holtec
- The remote sends the **same code every press**.
- Capture once with CC1101, store it, replay it on demand. Fully works.
- Easy, reliable. This is the "ESP-GRABER / replay" category.

### 3b. Rolling-code (hopping-code) systems — e.g. KeeLoq (HCS200/300/301), Somfy RTS,
Nice FloR, CAME ATOMO, BFT Mitto, Chamberlain Security+
- The transmitted code **changes every press** via a cryptographic counter + secret key.
- A naive capture-and-replay **DOES NOT WORK**: the receiver rejects a reused or
  out-of-window code. (Confirmed across multiple sources during research.)
- To act as a valid remote you must EITHER:
  - **(A) Enroll into the receiver** using the receiver's learn button. The ESP32 picks
    its own serial + key (or emulates a supported protocol), transmits a learn sequence,
    and the receiver adds it as an authorized remote. From then on the ESP32 generates
    valid rolling codes itself. This is legitimate and robust. Requires physical access
    to the receiver's program button (you own it, so fine). **Protocol-specific** — only
    works for protocols we implement (e.g. Somfy RTS, KeeLoq-with-known-manufacturer-key).
  - **(B) Know the secret/manufacturer key** to generate the next valid code. Generally
    not available for commercial openers; not a path we rely on.

> You cannot generically "duplicate" an arbitrary rolling-code remote by sniffing it.
> That is by design. Any product that claims otherwise is either fixed-code only or is
> enrolling as a new remote.

## 4. Assessment of the referenced library: ESP-GRABER (Teapot174)

- Platform: ESP32 + CC1101, 315/433/868/915 MHz. MIT licensed. **Repo archived (read-only)
  as of Aug 2026, author states no further updates** — not a maintainable dependency.
- It is a **standalone handheld tool** with its own OLED display + 4 buttons UI. It is
  NOT a library you link against; it's a full firmware. It cannot be imported into our
  HomeSpan/HomeKit firmware as-is.
- Supported modulations: Princeton, RcSwitch, CAME, Holtec, Nice, StarLine, KeeLoq.
- Includes a **jammer** feature which is **illegal** in most jurisdictions (incl. EU/US).
  We will NOT include or use any jamming capability.
- Its "signal read + signal send" is a **replay** model — great for fixed-code, and for
  understanding/decoding signals, but not a drop-in rolling-code enrollment solution.

**Verdict:** Useful as a *reference* for CC1101 register setup and OOK capture/replay and
for identifying your remote's protocol. Not suitable as a direct dependency (archived,
monolithic, handheld-UI, includes illegal jammer). We will build our own focused firmware
and borrow proven, maintained, single-purpose libraries instead.

## 5. Better-fit building blocks (maintained, single-purpose)

Depending on what YOUR remote turns out to be:

| Your remote type | Recommended approach | Candidate library |
|---|---|---|
| Fixed-code (PT2262/EV1527/CAME/Holtec) | Capture once, replay | `rc-switch`, or raw CC1101 OOK capture/replay (SmartRC-CC1101-Driver-Lib) |
| Somfy RTS (rolling) | Emulate as new remote, enroll via learn button | `Legion2/Somfy_Remote_Lib` (actively maintained) |
| KeeLoq (rolling) w/ known mfr key | Emulate as new remote, enroll | custom + known-key KeeLoq routines (e.g. as in esphome-came-moovo-gate) |
| CAME/Moovo/mHouse | Enroll as new remote | `mabt/esphome-came-moovo-gate` (reference) |
| Enjoy Motors | Emulate remote | `markstor/Enjoy_Remote_Lib` |

RF driver layer in all cases: **SmartRC-CC1101-Driver-Lib** (ELECHOUSE) — the de-facto
Arduino CC1101 driver, works on ESP32, supports raw TX/RX for OOK/ASK.

## 6. Hardware

| Component | Notes |
|---|---|
| ESP32-WROOM-32 DevKit | Same board family as the light project |
| CC1101 transceiver module | Must match your remote's band: **433.92 MHz** is most common in EU; some are 868 MHz; Somfy RTS is 433.42 MHz; US often 315 MHz. Buy the module variant for the right band, or a wideband one. |
| Antenna | A simple quarter-wave wire (e.g. ~17.3 cm for 433 MHz) tuned to the band |
| Dupont wires / proto board | SPI wiring |
| (Optional) status LED / button | Local "learn" trigger + status |

### CC1101 <-> ESP32 wiring (SPI) — proposed, finalized in design phase
Standard VSPI mapping (we will NOT reuse ESP-GRABER's exact pins blindly; GPIO2 conflicts
with onboard LED strapping — avoid for CS):

| CC1101 pin | ESP32 GPIO | Function |
|---|---|---|
| VCC | 3V3 | Power (CC1101 is 3.3 V only — do NOT use 5 V) |
| GND | GND | Ground |
| SCK | GPIO18 | SPI clock |
| MISO (SO) | GPIO19 | SPI MISO |
| MOSI (SI) | GPIO23 | SPI MOSI |
| CSN | GPIO5  | Chip select |
| GDO0 | GPIO16 | TX/RX data (async OOK) |
| GDO2 | GPIO17 | Secondary data / status (optional) |

## 7. Legal and safety constraints (hard requirements)

- **Only** for a garage/gate **you own or are explicitly authorized to operate**.
- **No jammer** functionality, ever.
- **No defeating** of another party's rolling-code security — we enroll as an authorized
  remote via the receiver's own learn button (physical access = proof of ownership).
- Garage doors are heavy and can injure people/pets. A software "open" must be deliberate;
  avoid designs that can open the door accidentally (e.g. on every reboot). HomeKit state
  must reflect reality as best we can, but note we may be "open-loop" (no position sensor)
  unless we add one.
- Comply with local RF regulations (duty cycle, band, power). CC1101 modules are low power,
  but transmitting on licensed bands or exceeding duty cycle may be restricted.

## 8. Key open questions to resolve before design sign-off

1. **What garage/gate is this?** Brand + model of the opener and the remote (photo of the
   remote's FCC ID / back label is ideal).
2. **Band and modulation?** 315 / 433.92 / 433.42 / 868 MHz? OOK/ASK or FSK?
3. **Fixed-code or rolling-code?** (Determines replay vs enroll.)
4. **Do you have physical access to the receiver's learn/program button?** (Required for
   rolling-code enroll path.)
5. **Do you want position feedback** (true open/closed state in HomeKit) via a reed/magnet
   sensor, or is a simple "momentary trigger" (push-to-toggle) acceptable for v1?
6. **Which band CC1101 module do you already have / will buy?**

Until Q1–Q3 are answered we cannot finalize whether v1 is a simple replay (easy) or a
protocol emulation + enroll (more work). The plan below is staged to handle both.
