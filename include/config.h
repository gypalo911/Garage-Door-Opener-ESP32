#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Hardware configuration — ESP32-WROOM-32 <-> CC1101 (VSPI)
// ---------------------------------------------------------------------------
// CC1101 is 3.3 V ONLY. Do not connect VCC to 5V.
//
//   CC1101 pin   ESP32 GPIO   Function
//   ----------   ----------   --------
//   VCC          3V3          Power (3.3 V)
//   GND          GND          Ground
//   SCK          GPIO18       SPI clock   (VSPI SCK)
//   MISO (SO)    GPIO19       SPI MISO    (VSPI MISO) -> also used as GDO1
//   MOSI (SI)    GPIO23       SPI MOSI    (VSPI MOSI)
//   CSN          GPIO5        Chip select (VSPI SS)
//   GDO0         GPIO25       Async data in/out (OOK) for RX capture / TX
//   GDO2         GPIO4        Secondary data/status (optional, used by lib)
//
// NOTE: SmartRC-CC1101-Driver-Lib uses the standard VSPI pins (18/19/23/5) by
// default. GDO0/GDO2 are set via ELECHOUSE_cc1101.setGDO0()/setGDO().
// ---------------------------------------------------------------------------

namespace cfg {

// CC1101 GDO pins. GDO0 is on GPIO25 (a safe, non-strapping pin) so flashing
// is never blocked. GDO2 on GPIO4. The proven TX path uses setGDO(gdo0, gdo2),
// so BOTH must be wired.
//   (SPI pins are the VSPI defaults: SCK18, MISO19, MOSI23, CS5)
constexpr uint8_t CC1101_GDO0 = 25;  // primary data pin (RX/TX of raw OOK)
constexpr uint8_t CC1101_GDO2 = 4;   // secondary (required by setGDO)

// Operating frequency for GANT-GM 800/3000.
constexpr float RF_FREQ_MHZ = 433.92f;

// Optional local controls (set to 255 to disable).
constexpr uint8_t LEARN_BUTTON_PIN = 255;  // optional push-button to enter learn mode
constexpr uint8_t STATUS_LED_PIN   = 255;  // optional status LED

// HomeKit pairing code (format NNN-NN-NNN when entered in Home app: 111-22-333).
constexpr char HOMEKIT_PAIRING_CODE[] = "11122333";
constexpr char DEVICE_NAME[]          = "ESP32 Garage Door";
constexpr char ACCESSORY_NAME[]       = "Garage Door";

// Open-loop door travel time (ms): how long we pretend the door takes to move,
// since v1 has no position sensor. Tune to your real door.
constexpr uint32_t DOOR_TRAVEL_MS = 15000;

}  // namespace cfg
