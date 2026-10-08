#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "rf_backend.h"

// Fixed-code OOK capture + replay using a CC1101 in async (raw) mode.
//
// Approach (proven for PT2262/EV1527-style fixed-code remotes, which GANT-GM
// 800/3000 uses): put the CC1101 into ASK/OOK async mode with the data appearing
// on GDO0. During LEARN we timestamp the edges on GDO0 to record the pulse-train
// (durations in microseconds). During TRANSMIT we bit-bang those same durations
// back out on GDO0 with the CC1101 in TX, repeating the frame like a real remote.
//
// We store the raw timing array (not a decoded value), which makes it protocol
// agnostic for any fixed-code OOK remote.

class CC1101FixedCode : public RfBackend {
 public:
  // Max number of pulse durations we store for one frame (one key press burst,
  // single repetition). 512 covers typical 24-32 bit fixed-code frames w/ margin.
  static constexpr size_t MAX_EDGES = 512;

  CC1101FixedCode(uint8_t gdo0, uint8_t gdo2, float freqMHz);

  bool begin() override;
  bool learn(uint32_t timeout_ms) override;
  bool hasSignal() const override;
  bool transmit() override;
  // Test helper: transmit with inverted carrier polarity (diagnostics).
  bool transmitInverted();
  void forget() override;

  // Print the captured timing frame to Serial (diagnostics).
  void dumpSignal() const;

  // Non-destructive: capture a fresh frame into a temporary buffer, print it,
  // and compare it against the currently-stored signal. Does NOT modify the
  // stored signal or NVS. Returns true if a fresh frame was captured.
  bool captureAndCompare(uint32_t timeout_ms);

 private:
  // Radio mode helpers
  void enterRxAsync();
  void enterTxAsync();
  void idle();

  // Shared raw capture: fills out[] with up to MAX_EDGES pulse durations.
  // Returns the number of pulses captured (0 if none/timeout).
  size_t captureFrame(uint16_t *out, bool *outFirstHigh, uint32_t timeout_ms);

  // Persistence
  bool loadFromNvs();
  void saveToNvs();

  uint8_t gdo0_;
  uint8_t gdo2_;
  float freqMHz_;

  // Captured frame: alternating HIGH/LOW pulse durations in microseconds.
  uint16_t durations_[MAX_EDGES];
  size_t edgeCount_ = 0;
  bool firstLevelHigh_ = true;   // level of the first recorded pulse
  uint8_t repeatCount_ = 8;      // how many times to resend on transmit

  Preferences prefs_;
};
