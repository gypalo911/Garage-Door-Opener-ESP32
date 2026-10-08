#pragma once
#include <Arduino.h>

// Abstract RF backend. v1 implementation is fixed-code capture + replay on CC1101.
// Kept as an interface so a protocol-emulator backend could be swapped in later.
class RfBackend {
 public:
  virtual ~RfBackend() = default;

  // Initialize the radio hardware. Returns false if the CC1101 is not detected.
  virtual bool begin() = 0;

  // Enter capture/"learn" mode and block (with timeout) until a fixed-code frame
  // is captured, then persist it. Returns true if a frame was captured & stored.
  virtual bool learn(uint32_t timeout_ms) = 0;

  // True if a learned signal is available (in RAM or restored from NVS).
  virtual bool hasSignal() const = 0;

  // Transmit the stored signal (replays the captured fixed-code frame).
  // Returns false if no signal is stored.
  virtual bool transmit() = 0;

  // Forget the stored signal (clears RAM + NVS).
  virtual void forget() = 0;
};
