# Implementation Plan — HomeKit Garage Door Opener (ESP32 + CC1101)

Staged plan. Each phase ends in something testable. We do NOT write the protocol code
until Phase 0 tells us which path (replay vs emulate) your actual door needs.

---

## Phase 0 — Identify the door & decide the path  (NO code; blocking)
**Goal:** Know exactly what we're transmitting to.
- [ ] 0.1 Identify opener/remote brand + model (photo of remote label / FCC ID).
- [ ] 0.2 Determine band (315 / 433.42 / 433.92 / 868 MHz) and modulation (OOK/ASK vs FSK).
- [ ] 0.3 Determine fixed-code vs rolling-code.
- [ ] 0.4 Confirm you have physical access to the receiver's "learn/program" button.
- [ ] 0.5 **Decision gate:**
  - Fixed-code → Phase 2A (replay).
  - Rolling-code, supported protocol (e.g. Somfy RTS) → Phase 2B (emulate + enroll).
  - Rolling-code, unsupported → Phase 2C fallback (drive a real spare remote via a relay/
    optocoupler on a GPIO — 100% reliable, protocol-agnostic, cheap). Recommended fallback.
- [ ] 0.6 Confirm you have (or will order) the correct-band CC1101 module (or a relay +
      spare remote for 2C).

**Exit:** A one-line decision: "v1 path = 2A / 2B / 2C".

---

## Phase 1 — Project scaffold + HomeKit shell  (independent of RF path)
**Goal:** A HomeKit Garage Door tile that pairs and logs intent — no RF yet.
- [ ] 1.1 Create PlatformIO project (`platform=espressif32`, `board=esp32dev`,
      `framework=arduino`, `board_build.partitions=min_spiffs.csv`, `monitor_speed=115200`).
- [ ] 1.2 Add HomeSpan dependency.
- [ ] 1.3 Implement `GarageDoorOpener` service skeleton (CurrentDoorState, TargetDoorState,
      ObstructionDetected). On target change, log "would transmit OPEN/CLOSE" and simulate
      the open→opening→open transition (open-loop).
- [ ] 1.4 Enforce NFR1: boot state = Closed (assumed), no actuation on boot.
- [ ] 1.5 Build, upload, provision WiFi over serial, pair in Home app, verify the tile
      animates and logs intent.

**Exit (testable):** Garage tile in Home app toggles and logs intent over serial; no RF.

---

## Phase 2 — RF backend  (pick ONE lane per Phase 0 decision)

### Phase 2A — Fixed-code replay
- [ ] 2A.1 Wire CC1101 (SPI + GDO0), add `SmartRC-CC1101-Driver-Lib`.
- [ ] 2A.2 Bring-up: confirm SPI comms (read CC1101 version register).
- [ ] 2A.3 Implement CC1101 RX capture of the remote's OOK frame (timings + payload).
- [ ] 2A.4 "Learn" serial command: capture + store frame in NVS.
- [ ] 2A.5 Implement replay TX with correct timing + repeat count.
- [ ] 2A.6 Wire replay into `DoorController` so HomeKit "open" transmits the frame.

### Phase 2B — Rolling-code emulate + enroll (e.g. Somfy RTS)
- [ ] 2B.1 Wire CC1101, add driver + chosen protocol lib (e.g. Somfy_Remote_Lib).
- [ ] 2B.2 Generate a device identity (virtual remote serial) + persist rolling counter in NVS.
- [ ] 2B.3 Implement enroll ("PROG") transmission; press receiver learn button to pair.
- [ ] 2B.4 Implement normal command TX (open/stop/close) with incrementing persisted counter.
- [ ] 2B.5 Wire into `DoorController`; verify counter persists across reboot (AC4).

### Phase 2C — Relay fallback (protocol-agnostic, most reliable)
- [ ] 2C.1 Wire a 3.3 V relay / optocoupler to a GPIO; splice across a spare OEM remote's
      button (or power it from the ESP board).
- [ ] 2C.2 `DoorController` "open" = pulse GPIO ~300–500 ms to "press" the remote.
- [ ] 2C.3 No RF stack needed; works with ANY opener because it uses a genuine remote.

**Exit (testable):** HomeKit "open" physically operates YOUR real door.

---

## Phase 3 — Persistence, state & safety hardening
- [ ] 3.1 Robust NVS persistence (frame or rolling counter); verify across power-cycle (AC4).
- [ ] 3.2 Debounce / rate-limit transmits; prevent double-fire.
- [ ] 3.3 Open-loop state machine timing (opening→open, closing→closed) tuned to door.
- [ ] 3.4 (Optional FR8b) Add reed switch on GPIO for real position; report true state +
      ObstructionDetected on timeout.
- [ ] 3.5 Re-verify no boot-time actuation (NFR1).

**Exit:** Reliable, safe operation; state reflects reality (or sane open-loop assumption).

---

## Phase 4 — Docs, polish, release
- [ ] 4.1 README: wiring diagram, pairing steps, learn/enroll procedure, disclaimer (legal).
- [ ] 4.2 Record final CC1101 pin map and partition/footprint numbers.
- [ ] 4.3 Pairing/setup code documented; serial provisioning steps.
- [ ] 4.4 Final end-to-end acceptance pass against AC1–AC6.

**Exit:** Reproducible build + documented procedure; all acceptance criteria met.

---

## Suggested milestones
- **M1:** Phase 1 done — Garage tile pairs & animates (no RF). Fast, low-risk, demoable.
- **M2:** Phase 2 (chosen lane) — door physically opens from HomeKit.
- **M3:** Phase 3 — robust + safe + persistent.
- **M4:** Phase 4 — documented release.

## Phase 0 — RESOLVED (user input 2026-10-07)

- Opener brand: **GANT** (gate automation).
- Band: **433.92 MHz**.
- Code type: **assumed rolling code** (GANT ships both older fixed-code and newer KeeLoq
  rolling-code remotes; treat as rolling until the specific remote model is confirmed).
- "Can you reach the receiver's learn/program button?": TBD — but required for the RF
  enroll path. GANT receivers have a programming button on the control board, same as the
  factory procedure for adding a spare remote.

### DECISION LOCKED (2026-10-07): GANT-GM 800/3000, fixed code -> Phase 2A capture+replay
- Opener confirmed: **GANT-GM 800/3000** at **433.92 MHz**.
- Code type: **FIXED CODE** — inferred with high confidence from the user's BMW HomeLink
  working via pure capture-and-replay (no gate-unit learn button needed). Capture-replay
  only works on fixed-code remotes; rolling code would have required a gate-box button press
  and would reject replays.
- Final confirmation check: BMW HomeLink button still opens the gate reliably on repeated
  use. If it ever needed a gate-unit button or stopped after programming -> treat as rolling
  code and fall back to Phase 2C (relay + spare remote).
- **v1 path = Phase 2A: CC1101 capture + replay on 433.92 MHz** (direct equivalent of the
  BMW HomeLink method, exposed as a HomeKit GarageDoorOpener tile).
- Hardware to acquire: CC1101 433 MHz module + ~17.3 cm quarter-wave antenna wire.

### NEW EVIDENCE (2026-10-07): BMW HomeLink learned it by capture-and-replay
User reports BMW was programmed by: enter learn mode → press GANT remote → BMW captured
the signal → assign to a button. This is **capture-and-replay learning**, which per
HomeLink's own docs works for **fixed-code** devices (indicator goes solid, button works
immediately). For rolling code, HomeLink additionally requires pressing the LEARN button on
the gate motor ("one extra trip to the operator's learn button"). 

**Disambiguating question:** When programming the BMW, did the user have to press a button
on the GANT gate motor/control box?
- NO (worked from remote alone) → GANT remote is **FIXED CODE** → use **Phase 2A (capture +
  replay with CC1101)** — the direct electronic equivalent of what BMW does. PREFERRED.
- YES (had to press gate-unit button) → rolling code → Phase 2C relay fallback.

### Decision for v1 (pending the one disambiguating answer)
- If fixed code (likely): **Phase 2A — CC1101 capture-and-replay**. Mirrors BMW exactly.
- If rolling code: **Phase 2C — relay + genuine GANT remote**.

### (previous default) Phase 2C — relay + genuine GANT remote
Rationale:
- Research confirms rolling code **cannot be copied/replayed**. The only RF path is to
  **emulate + enroll** a supported protocol — and GANT/KeeLoq rolling is not a cleanly
  supported, maintained Arduino library (even Flipper's stock firmware won't save/replay
  GANT rolling codes; community firmware only emulates a limited set).
- A **relay/optocoupler wired across the button of a spare GANT remote** is:
  - legal, requires no reverse engineering,
  - works 100% regardless of fixed vs rolling code (it uses a genuine, already-enrolled
    remote — the rolling code is produced by the real remote chip),
  - the exact logical equivalent of a finger pressing the remote.
- This is the simplest reliable answer to the user's "as simple as possible" (Q5).
- Needs: one spare GANT remote (already paired to the gate, or paired once via the gate's
  normal procedure) + a 3.3 V relay or optocoupler module.

### Alternative (v2, more work): Phase 2B pure-RF KeeLoq emulate + enroll
Only if the user wants a no-extra-remote solution AND is willing to accept protocol
reverse-engineering risk. Requires identifying GANT's exact KeeLoq variant + manufacturer
key, which may not be publicly available. Higher risk, not recommended for v1.

## Immediate next action
Answer Phase 0, questions 0.1–0.4 (what is your garage opener/remote, band, and is it
fixed- or rolling-code, and can you reach the receiver's learn button). With that, I'll
lock the lane (2A/2B/2C), finalize the library + pin choices, and we can start building
Phase 1 immediately (Phase 1 is independent and can begin in parallel regardless).

## My recommendation
Start **Phase 1 now** (HomeKit shell — zero RF risk, reuses what we proved on the light).
In parallel, you gather Phase 0 info. For the RF lane, unless your door is confirmed
fixed-code, I recommend **Phase 2C (relay + spare remote)** for v1: it is legal, trivially
reliable, works with ANY opener including rolling-code, and sidesteps protocol reverse
engineering. We can pursue pure-RF emulation (2B) as a v2 once v1 works.
