#include "cc1101_fixedcode.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Timing bounds for fixed-code OOK capture (microseconds).
// Fixed-code remotes (PT2262/EV1527 family) use pulse widths roughly in the
// 100 us .. 20 ms range. Anything outside is treated as noise / inter-frame gap.
static constexpr uint32_t MIN_PULSE_US = 100;
static constexpr uint32_t MAX_PULSE_US = 20000;
// A gap longer than this marks the end of a frame burst (sync gap).
static constexpr uint32_t FRAME_GAP_US = 8000;
// Inter-frame gap inserted between repeated transmissions.
static constexpr uint32_t TX_GAP_US = 10000;
// Minimum edges to consider it a real frame (not noise).
static constexpr size_t MIN_EDGES = 24;

static const char* NVS_NAMESPACE = "garage";

// --- ISR edge-capture state (single instance; the device has one CC1101) -----
// We timestamp every edge on GDO0 in an ISR for accurate timing, far more
// reliable than polling digitalRead() in a loop.
namespace {
volatile uint32_t isrTimes[CC1101FixedCode::MAX_EDGES + 4];
volatile size_t   isrCount = 0;
volatile bool     isrFirstLevelHigh = false;
volatile bool     isrArmed = false;
uint8_t           isrPin = 0;

void IRAM_ATTR edgeIsr() {
  if (!isrArmed) return;
  uint32_t now = micros();
  size_t c = isrCount;
  if (c == 0) {
    // record level of the very first edge's resulting state
    isrFirstLevelHigh = (digitalRead(isrPin) == HIGH);
  }
  if (c < (CC1101FixedCode::MAX_EDGES + 4)) {
    isrTimes[c] = now;
    isrCount = c + 1;
  }
}
}  // namespace

CC1101FixedCode::CC1101FixedCode(uint8_t gdo0, uint8_t gdo2, float freqMHz)
    : gdo0_(gdo0), gdo2_(gdo2), freqMHz_(freqMHz) {}

bool CC1101FixedCode::begin() {
  ELECHOUSE_cc1101.setGDO0(gdo0_);
  ELECHOUSE_cc1101.Init();

  if (!ELECHOUSE_cc1101.getCC1101()) {
    return false;  // SPI comms / version check failed -> module not detected
  }

  // --- Raw async OOK configuration (critical for fixed-code capture/replay) ---
  // setCCMode(0) puts GDO0 into IOCFG=0x0D (async serial data output) and
  // PKTCTRL0=0x32 (asynchronous serial mode) — without this GDO0 never carries
  // the demodulated bitstream and capture reads nothing.
  ELECHOUSE_cc1101.setCCMode(0);       // async serial mode (not packet mode)
  ELECHOUSE_cc1101.setModulation(2);   // 2 = ASK/OOK
  ELECHOUSE_cc1101.setMHZ(freqMHz_);
  ELECHOUSE_cc1101.setRxBW(256.0f);    // wide RX bandwidth for OOK bursts
  ELECHOUSE_cc1101.setPA(12);          // max TX power (dBm) for replay
  idle();

  loadFromNvs();
  return true;
}

void CC1101FixedCode::idle() { ELECHOUSE_cc1101.setSidle(); }

// We fully (re)configure the radio for each direction, because async RX and
// bit-banged OOK TX need different register setups and sharing one config
// (the earlier bug) produced capture-but-no-usable-TX.

void CC1101FixedCode::enterRxAsync() {
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setCCMode(0);        // async serial: GDO0 outputs demod data
  ELECHOUSE_cc1101.setModulation(2);    // ASK/OOK
  ELECHOUSE_cc1101.setMHZ(freqMHz_);
  ELECHOUSE_cc1101.setRxBW(256.0f);
  pinMode(gdo0_, INPUT);
  ELECHOUSE_cc1101.SetRx();
}

void CC1101FixedCode::enterTxAsync() {
  // Proven TX recipe (from alx2k / simondankelmann working projects):
  // Init -> setGDO(gdo0,gdo2) [sets GDO0 as OUTPUT, ESP drives it] -> setMHZ ->
  // SetTx -> setModulation(ASK/OOK) -> setDRate(512) -> setPktFormat(3) async.
  // Then bit-bang GDO0 (carrier on when pin HIGH).
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setGDO(gdo0_, gdo2_);
  ELECHOUSE_cc1101.setMHZ(freqMHz_);
  ELECHOUSE_cc1101.SetTx();
  ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK
  ELECHOUSE_cc1101.setDRate(512);      // data rate used by the working project
  ELECHOUSE_cc1101.setPktFormat(3);    // 3 = asynchronous serial; data in on GDO0
  pinMode(gdo0_, OUTPUT);
  digitalWrite(gdo0_, LOW);
}

size_t CC1101FixedCode::captureFrame(uint16_t *out, bool *outFirstHigh,
                                     uint32_t timeout_ms) {
  enterRxAsync();

  // Arm the ISR edge timestamper.
  isrPin = gdo0_;
  isrCount = 0;
  isrArmed = true;
  attachInterrupt(digitalPinToInterrupt(gdo0_), edgeIsr, CHANGE);

  // Collect edges until we see a complete burst framed by a long gap, or timeout.
  const uint32_t startMs = millis();
  while (millis() - startMs < timeout_ms) {
    size_t c = isrCount;
    if (c >= MIN_EDGES) {
      uint32_t sinceLastUs = micros() - isrTimes[c - 1];
      if (sinceLastUs > FRAME_GAP_US) break;  // burst complete
    }
    if (c >= MAX_EDGES) break;
    delay(2);  // lets WiFi/HomeKit run; edges are caught by the ISR regardless
  }

  isrArmed = false;
  detachInterrupt(digitalPinToInterrupt(gdo0_));
  idle();

  size_t c = isrCount;
  if (c < MIN_EDGES + 1) return 0;

  size_t n = 0;
  for (size_t i = 1; i < c && n < MAX_EDGES; ++i) {
    uint32_t dur = isrTimes[i] - isrTimes[i - 1];
    if (dur < MIN_PULSE_US || dur > MAX_PULSE_US) {
      if (n >= MIN_EDGES) break;  // already have a full frame
      n = 0;                      // else resync
      continue;
    }
    out[n++] = (uint16_t)dur;
  }

  if (n >= MIN_EDGES) {
    if (outFirstHigh) *outFirstHigh = isrFirstLevelHigh;
    return n;
  }
  return 0;
}

bool CC1101FixedCode::learn(uint32_t timeout_ms) {
  bool firstHigh = true;
  size_t n = captureFrame(durations_, &firstHigh, timeout_ms);
  if (n >= MIN_EDGES) {
    edgeCount_ = n;
    firstLevelHigh_ = firstHigh;
    saveToNvs();
    return true;
  }
  edgeCount_ = 0;
  return false;
}

bool CC1101FixedCode::captureAndCompare(uint32_t timeout_ms) {
  uint16_t temp[MAX_EDGES];
  bool firstHigh = true;
  size_t n = captureFrame(temp, &firstHigh, timeout_ms);
  if (n < MIN_EDGES) {
    Serial.println(">>> No signal captured (timeout). Stored signal untouched.");
    return false;
  }

  Serial.printf(">>> Fresh capture: %u pulses, firstLevel=%s\n",
                (unsigned)n, firstHigh ? "HIGH" : "LOW");
  for (size_t i = 0; i < n; ++i) {
    Serial.printf("%u ", temp[i]);
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();

  if (!hasSignal()) {
    Serial.println(">>> No stored signal to compare against.");
    return true;
  }

  // Compare: same count? and element-wise closeness (fixed code => same code,
  // but with timing jitter, so allow a tolerance band).
  Serial.printf(">>> Stored: %u pulses, firstLevel=%s\n",
                (unsigned)edgeCount_, firstLevelHigh_ ? "HIGH" : "LOW");

  size_t cmpLen = (n < edgeCount_) ? n : edgeCount_;
  size_t within = 0, total = 0;
  uint32_t sumAbsDiff = 0;
  for (size_t i = 0; i < cmpLen; ++i) {
    int diff = (int)temp[i] - (int)durations_[i];
    uint32_t ad = diff < 0 ? -diff : diff;
    sumAbsDiff += ad;
    total++;
    // "close" = within 150us OR within 30% of the stored value
    uint32_t tol = durations_[i] * 30 / 100;
    if (tol < 150) tol = 150;
    if (ad <= tol) within++;
  }
  Serial.printf(">>> Match: %u/%u pulses within tolerance; avg jitter %u us; "
                "lengths %s (fresh=%u vs stored=%u)\n",
                (unsigned)within, (unsigned)total,
                total ? (unsigned)(sumAbsDiff / total) : 0,
                (n == edgeCount_) ? "EQUAL" : "DIFFER",
                (unsigned)n, (unsigned)edgeCount_);
  if (within == total && total > 0) {
    Serial.println(">>> VERDICT: SAME code (fixed-code confirmed; only timing jitter).");
  } else {
    Serial.println(">>> VERDICT: frames differ beyond jitter — may be a mis-aligned "
                   "capture start, a different button, or noise. Try again.");
  }
  return true;
}

bool CC1101FixedCode::hasSignal() const { return edgeCount_ >= MIN_EDGES; }

bool CC1101FixedCode::transmit() {
  if (!hasSignal()) return false;
  enterTxAsync();
  bool startHigh = firstLevelHigh_;
  noInterrupts();
  for (uint8_t r = 0; r < repeatCount_; ++r) {
    int level = startHigh ? HIGH : LOW;
    for (size_t i = 0; i < edgeCount_; ++i) {
      digitalWrite(gdo0_, level);
      delayMicroseconds(durations_[i]);
      level = (level == HIGH) ? LOW : HIGH;
    }
    digitalWrite(gdo0_, LOW);
    interrupts();
    delayMicroseconds(TX_GAP_US);
    noInterrupts();
  }
  interrupts();
  digitalWrite(gdo0_, LOW);
  idle();
  return true;
}

bool CC1101FixedCode::transmitInverted() {
  if (!hasSignal()) return false;
  enterTxAsync();
  bool startHigh = !firstLevelHigh_;   // inverted polarity
  noInterrupts();
  for (uint8_t r = 0; r < repeatCount_; ++r) {
    int level = startHigh ? HIGH : LOW;
    for (size_t i = 0; i < edgeCount_; ++i) {
      digitalWrite(gdo0_, level);
      delayMicroseconds(durations_[i]);
      level = (level == HIGH) ? LOW : HIGH;
    }
    digitalWrite(gdo0_, LOW);
    interrupts();
    delayMicroseconds(TX_GAP_US);
    noInterrupts();
  }
  interrupts();
  digitalWrite(gdo0_, LOW);
  idle();
  return true;
}

void CC1101FixedCode::dumpSignal() const {
  if (!hasSignal()) {
    Serial.println("(no signal stored)");
    return;
  }
  Serial.printf("Captured %u pulses, firstLevel=%s:\n",
                (unsigned)edgeCount_, firstLevelHigh_ ? "HIGH" : "LOW");
  for (size_t i = 0; i < edgeCount_; ++i) {
    Serial.printf("%u ", durations_[i]);
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();
}

void CC1101FixedCode::forget() {
  edgeCount_ = 0;
  prefs_.begin(NVS_NAMESPACE, false);
  prefs_.remove("edges");
  prefs_.remove("count");
  prefs_.remove("first");
  prefs_.end();
}

bool CC1101FixedCode::loadFromNvs() {
  prefs_.begin(NVS_NAMESPACE, true);
  size_t count = prefs_.getUInt("count", 0);
  if (count == 0 || count > MAX_EDGES) {
    prefs_.end();
    edgeCount_ = 0;
    return false;
  }
  size_t bytes = prefs_.getBytesLength("edges");
  if (bytes != count * sizeof(uint16_t)) {
    prefs_.end();
    edgeCount_ = 0;
    return false;
  }
  prefs_.getBytes("edges", durations_, bytes);
  firstLevelHigh_ = prefs_.getBool("first", true);
  prefs_.end();
  edgeCount_ = count;
  return true;
}

void CC1101FixedCode::saveToNvs() {
  prefs_.begin(NVS_NAMESPACE, false);
  prefs_.putBytes("edges", durations_, edgeCount_ * sizeof(uint16_t));
  prefs_.putUInt("count", (uint32_t)edgeCount_);
  prefs_.putBool("first", firstLevelHigh_);
  prefs_.end();
}
