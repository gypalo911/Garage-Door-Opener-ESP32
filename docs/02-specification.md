# Specification — HomeKit Garage Door Opener (ESP32 + CC1101)

## Overview
A HomeKit-connected device that triggers a garage/gate door opener by transmitting the
appropriate sub-GHz RF signal through a CC1101 module, controlled from the Apple Home app
and Siri. Same control pattern as the existing HomeSpan "Blue Light" project, with an RF
transmitter as the actuator instead of an LED.

## Scope

### In scope (v1)
- HomeKit accessory exposing garage door control.
- RF transmit via CC1101 at the correct band/modulation for the user's door.
- At least one of:
  - **Replay mode** for fixed-code remotes (capture once, replay on command), and/or
  - **Emulation/enroll mode** for a supported rolling-code protocol (e.g. Somfy RTS).
- WiFi credential provisioning over serial (reuse HomeSpan approach — no secrets in code).
- Persisting the learned signal / rolling-code counter in NVS so it survives reboots.
- A "learn" workflow to capture a fixed-code signal or to enroll into a receiver.

### Out of scope (v1)
- Jamming / denial of service (explicitly forbidden).
- Cracking or brute-forcing unknown rolling-code keys.
- Supporting every protocol at once — v1 targets the user's actual door only.
- Multi-door management UI (can be a later epic).
- A physical display/button UI (ESP-GRABER-style). Control is via HomeKit; serial for setup.

## Functional requirements

- **FR1 — HomeKit integration.** Device appears in the Home app and is controllable via
  app + Siri. Uses HomeSpan on ESP32 (consistent with existing project).
- **FR2 — Accessory type.** Expose as HomeKit `GarageDoorOpener` service
  (CurrentDoorState, TargetDoorState, ObstructionDetected) — the semantically correct type
  so Home shows a garage tile. If no position sensor exists, state is modeled open-loop
  (see FR8).
- **FR3 — Trigger transmit.** On "open"/"close" target change, transmit the stored/generated
  RF frame(s) with correct timing, repeat count, and inter-frame gap for the protocol.
- **FR4 — Fixed-code replay.** Ability to capture a fixed-code remote's frame via CC1101 RX
  and replay it on command.
- **FR5 — Rolling-code emulation (protocol-specific).** For a supported protocol, generate
  valid rolling codes using a device serial + incrementing counter, enrollable into the
  receiver via its learn button.
- **FR6 — Learn/provision workflow.** A serial-triggered (and optionally button-triggered)
  mode to (a) capture a fixed-code signal, or (b) emit an enroll sequence for the receiver.
- **FR7 — Persistence.** Store captured frame OR {serial, rolling counter, keys} in ESP32
  NVS; rolling counter must monotonically persist across reboots to stay in sync.
- **FR8 — State handling.** v1 may be open-loop (no door sensor): HomeKit shows a momentary
  transition then settles to an assumed state. OPTIONAL FR8b: wired reed switch on a GPIO
  for true open/closed feedback and obstruction/timeout handling.
- **FR9 — WiFi provisioning.** Reuse HomeSpan 'W' serial provisioning; no hardcoded creds.
- **FR10 — Config/pairing code.** HomeKit setup code configurable; documented pairing steps.

## Non-functional requirements

- **NFR1 — Safety.** No action on boot that could open the door unintentionally. Target
  state changes must be explicit HomeKit commands. Default state after boot = "Closed/assumed".
- **NFR2 — Legality.** No jammer. Operates only on the user's own door. Respects local RF
  band/duty-cycle rules. Ships with a disclaimer.
- **NFR3 — Reliability.** RF timing accurate enough for the target protocol; retransmit N
  times like a real remote. Rolling counter never goes backwards.
- **NFR4 — Security.** HomeKit pairing protects remote access. Captured codes / keys stored
  in NVS, not exposed over the air or logged in plaintext beyond what's needed for debug.
- **NFR5 — Footprint.** Fit in the `min_spiffs.csv` partition (we already enabled ~1.9 MB
  app space in the light project; use the same scheme here).
- **NFR6 — Maintainability.** Clean separation: `HomeKitService` <-> `DoorController` <->
  `RfBackend` (fixed-replay or protocol-emulator) <-> `CC1101Driver`. Swappable backends.
- **NFR7 — Power.** CC1101 is 3.3 V only. No 5 V on the module.

## Hardware requirements
- ESP32-WROOM-32 DevKit.
- CC1101 module matching the door's band (315 / 433.42 / 433.92 / 868 MHz).
- Quarter-wave antenna wire for the chosen band.
- SPI wiring per design (CSN, SCK, MISO, MOSI, GDO0, [GDO2]); 3V3 + GND.
- Optional: reed switch (door position) + magnet; status LED.

## Architecture (layers)

```
Apple Home app / Siri
        │  (HAP over WiFi)
   HomeSpan GarageDoorOpener service   <- FR1,FR2
        │
   DoorController (state machine, timing, persistence)   <- FR3,FR7,FR8
        │
   RfBackend (interface)
     ├── FixedCodeReplayBackend      <- FR4
     └── ProtocolEmulatorBackend     <- FR5  (e.g. SomfyRtsBackend)
        │
   CC1101Driver (SmartRC-CC1101-Driver-Lib, SPI + GDO0 OOK)   <- NFR7
        │
     CC1101 module  ───((( RF )))───►  garage receiver
```

## Acceptance criteria (v1)
- AC1: Device pairs in Apple Home and shows a Garage Door tile.
- AC2: Tapping "open" (and/or "close") in Home transmits RF that actually operates the
  user's real door.
- AC3: For the user's door type, the correct backend is used (replay OR emulate+enroll),
  verified by the door responding.
- AC4: After an ESP32 reboot/power-cycle, the device still operates the door (persistence
  works; rolling counter preserved and still accepted).
- AC5: No build-in jammer, no boot-time door actuation. Disclaimer present in README.
- AC6: Firmware fits the partition; WiFi provisioned over serial; pairing documented.

## Dependencies / libraries (candidates — finalized after door identification)
- `HomeSpan` (HomeKit) — already proven in the light project.
- `SmartRC-CC1101-Driver-Lib` (ELECHOUSE) — CC1101 SPI/OOK driver.
- Protocol libs as applicable: `Legion2/Somfy_Remote_Lib`, `rc-switch`, or custom KeeLoq.
- Reference only (not linked): `Teapot174/ESP-GRABER`, `mabt/esphome-came-moovo-gate`.

## Risks
- R1: Door is rolling-code of an **unsupported** protocol → replay impossible, emulation
  not available → may require buying a compatible spare remote to drive via relay instead
  (fallback design, see plan Phase 0 decision).
- R2: Wrong CC1101 band module → no range/no function. Mitigate by identifying band first.
- R3: Rolling counter desync (if counter not persisted correctly) → door stops responding
  until re-enrolled. Mitigate with robust NVS writes + margin.
- R4: Open-loop state can misreport in HomeKit. Mitigate with optional reed sensor (FR8b).
