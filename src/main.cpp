#include <Arduino.h>
#include "HomeSpan.h"
#include "config.h"
#include "cc1101_fixedcode.h"

// ---------------------------------------------------------------------------
// RF backend (fixed-code capture + replay on CC1101 @ 433.92 MHz for GANT-GM).
// ---------------------------------------------------------------------------
static CC1101FixedCode rf(cfg::CC1101_GDO0, cfg::CC1101_GDO2, cfg::RF_FREQ_MHZ);
static bool rfReady = false;

// HomeKit GarageDoorOpener current/target door state values (HAP spec):
//   0 = Open, 1 = Closed, 2 = Opening, 3 = Closing, 4 = Stopped
enum DoorState : uint8_t { OPEN = 0, CLOSED = 1, OPENING = 2, CLOSING = 3, STOPPED = 4 };

struct GarageDoor : Service::GarageDoorOpener {
  SpanCharacteristic *current;      // CurrentDoorState
  SpanCharacteristic *target;       // TargetDoorState
  SpanCharacteristic *obstruction;  // ObstructionDetected

  uint32_t moveStartMs = 0;
  bool moving = false;

  GarageDoor() : Service::GarageDoorOpener() {
    // NFR1: boot assumed CLOSED; never actuate on boot.
    current = new Characteristic::CurrentDoorState(CLOSED);
    target = new Characteristic::TargetDoorState(CLOSED);
    obstruction = new Characteristic::ObstructionDetected(false);
  }

  // Fire the RF remote (same signal for open & close — GANT uses one toggle button).
  void pressRemote() {
    if (rfReady && rf.hasSignal()) {
      bool ok = rf.transmit();
      LOG0("RF transmit %s\n", ok ? "OK" : "FAILED");
    } else {
      LOG0("*** No learned RF signal yet — type 'L' to learn. (Simulating.)\n");
    }
  }

  boolean update() override {
    uint8_t tgt = target->getNewVal();

    // Only act on a real change of target.
    if (tgt == OPEN) {
      LOG0("HomeKit: OPEN requested\n");
      pressRemote();
      current->setVal(OPENING);
      moveStartMs = millis();
      moving = true;
    } else if (tgt == CLOSED) {
      LOG0("HomeKit: CLOSE requested\n");
      pressRemote();
      current->setVal(CLOSING);
      moveStartMs = millis();
      moving = true;
    }
    return true;
  }

  void loop() override {
    // Open-loop: after DOOR_TRAVEL_MS, settle current state to the target.
    if (moving && (millis() - moveStartMs >= cfg::DOOR_TRAVEL_MS)) {
      uint8_t tgt = target->getVal();
      current->setVal(tgt == OPEN ? OPEN : CLOSED);
      moving = false;
      LOG0("Door settled to %s\n", tgt == OPEN ? "OPEN" : "CLOSED");
    }
  }
};

static GarageDoor *garage = nullptr;

// Serial command: 'L' + <return> enters RF learn mode.
void learnCommand(const char *buf) {
  if (!rfReady) {
    Serial.println("CC1101 not detected — cannot learn.");
    return;
  }
  Serial.println(">>> LEARN MODE: press your GANT remote now (15 s)...");
  bool ok = rf.learn(15000);
  Serial.println(ok ? ">>> Signal captured & saved!" : ">>> No signal captured (timeout).");
  if (ok) rf.dumpSignal();
}

void setup() {
  Serial.begin(115200);

  // Bring up the CC1101. Non-fatal if missing: HomeKit tile still works (sim mode).
  rfReady = rf.begin();
  Serial.printf("CC1101 init: %s\n", rfReady ? "OK" : "NOT DETECTED");
  Serial.printf("Stored RF signal: %s\n", rf.hasSignal() ? "YES (ready to replay)" : "none yet");

  homeSpan.setPairingCode(cfg::HOMEKIT_PAIRING_CODE);
  homeSpan.begin(Category::GarageDoorOpeners, cfg::DEVICE_NAME);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name(cfg::ACCESSORY_NAME);

  garage = new GarageDoor();

  // Serial console commands — invoke with the HomeSpan user-command '@' prefix:
  //   @L learn, @T test-transmit, @D dump, @F forget.
  new SpanUserCommand('L', "- learn/capture the GANT remote RF signal", [](const char *buf) {
    learnCommand(buf);
  });
  new SpanUserCommand('T', "- test-transmit the stored RF signal (fires the door)", [](const char *buf) {
    if (!rfReady) { Serial.println("CC1101 not detected."); return; }
    Serial.println(rf.transmit() ? ">>> Transmitted." : ">>> No signal stored — learn first (@L).");
  });
  new SpanUserCommand('I', "- test-transmit with INVERTED carrier polarity (diagnostic)", [](const char *buf) {
    if (!rfReady) { Serial.println("CC1101 not detected."); return; }
    Serial.println(rf.transmitInverted() ? ">>> Transmitted (inverted)." : ">>> No signal stored.");
  });
  new SpanUserCommand('D', "- dump the stored RF signal timings", [](const char *buf) {
    rf.dumpSignal();
  });
  new SpanUserCommand('C', "- capture a fresh press and compare to stored (no overwrite)", [](const char *buf) {
    if (!rfReady) { Serial.println("CC1101 not detected."); return; }
    Serial.println(">>> COMPARE: press your GANT remote now (15 s)... (stored signal is NOT changed)");
    rf.captureAndCompare(15000);
  });
  new SpanUserCommand('F', "- forget/erase the stored RF signal", [](const char *buf) {
    rf.forget();
    Serial.println(">>> Stored RF signal erased.");
  });
}

void loop() {
  homeSpan.poll();
}
