#include <Arduino.h>
#include <Wire.h>
#include <pico/time.h>

#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>

#include <cmath>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "loadcell_config.h"
#include "loadcell_types.h"
#include "static_ring.h"

namespace loadcell {
namespace {

constexpr char kFirmwareName[] = "rp2040_nau7802_loadcell";
constexpr char kFirmwareVersion[] = "0.2.0";
constexpr uint8_t kProtocolVersion = 1;

struct ProtocolLine {
  char text[kProtocolLineCapacity];
  size_t length;
};

NAU7802 g_scale;
#if LOADCELL_I2C_INSTANCE == 0
TwoWire &g_wire = Wire;
#else
TwoWire &g_wire = Wire1;
#endif
StaticRing<OutputRecord, kOutputQueueCapacity> g_output_queue;
StaticRing<ProtocolLine, kResponseQueueCapacity> g_response_queue;

volatile DrdyEvent g_drdy_queue[kDrdyQueueCapacity]{};
volatile uint8_t g_drdy_head = 0U;
volatile uint8_t g_drdy_tail = 0U;
volatile uint32_t g_drdy_overrun_count = 0U;
volatile uint32_t g_edge_sequence = 0U;
volatile uint64_t g_last_drdy_timestamp_us = 0U;
volatile DrdyEvent g_latest_captured_drdy_event{};
volatile bool g_capture_edges = false;
DrdyEvent g_retry_drdy_event{};
bool g_retry_drdy_event_valid = false;
uint64_t g_next_ready_poll_us = 0U;

InstrumentState g_state = InstrumentState::kUnavailable;
HealthCounters g_health{};
RunCounters g_run{};
TareAccumulator g_tare{};

bool g_sensor_ready = false;
uint16_t g_sample_rate_sps = LOADCELL_DEFAULT_SAMPLE_RATE_SPS;
uint16_t g_gain = LOADCELL_DEFAULT_GAIN;
double g_last_tare_mean = 0.0;
double g_last_tare_stddev = 0.0;
uint32_t g_last_tare_count = 0U;

char g_command_line[kCommandLineCapacity]{};
size_t g_command_length = 0U;
bool g_discarding_long_command = false;

ProtocolLine g_active_tx_line{};
size_t g_active_tx_offset = 0U;
bool g_active_tx_valid = false;
bool g_active_tx_is_final = false;
bool g_final_enqueued = false;
bool g_usb_was_connected = false;
#if LOADCELL_LED_BLUE_CONFIGURED
bool g_blue_led_on = false;
#endif
#if LOADCELL_LED_RED_CONFIGURED
bool g_red_led_on = false;
#endif
#if LOADCELL_LED_GREEN_CONFIGURED
bool g_green_led_on = false;
#endif

void initializeIndicatorLeds() {
#if LOADCELL_LEDS_CONFIGURED
  const int off_level = LOADCELL_LED_ACTIVE_HIGH ? LOW : HIGH;
#endif
#if LOADCELL_LED_BLUE_CONFIGURED
  digitalWrite(LOADCELL_LED_BLUE_PIN, off_level);
  pinMode(LOADCELL_LED_BLUE_PIN, OUTPUT);
#endif
#if LOADCELL_LED_RED_CONFIGURED
  digitalWrite(LOADCELL_LED_RED_PIN, off_level);
  pinMode(LOADCELL_LED_RED_PIN, OUTPUT);
#endif
#if LOADCELL_LED_GREEN_CONFIGURED
  digitalWrite(LOADCELL_LED_GREEN_PIN, off_level);
  pinMode(LOADCELL_LED_GREEN_PIN, OUTPUT);
#endif
}

#if LOADCELL_LEDS_CONFIGURED
void setIndicatorLed(int pin, bool *current_state, bool requested_state) {
  if (*current_state == requested_state) {
    return;
  }
  const int on_level = LOADCELL_LED_ACTIVE_HIGH ? HIGH : LOW;
  digitalWrite(pin, requested_state ? on_level : !on_level);
  *current_state = requested_state;
}
#endif

void updateIndicatorLeds() {
#if LOADCELL_LEDS_CONFIGURED
  const bool active = (g_state == InstrumentState::kTaring) ||
                      (g_state == InstrumentState::kRunning) ||
                      (g_state == InstrumentState::kStopping);
  const bool integrity_fault =
      !g_sensor_ready || (g_state == InstrumentState::kUnavailable) ||
      (g_health.dropped_samples != 0U) || (g_health.i2c_errors != 0U) ||
      (g_health.buffer_overruns != 0U) ||
      (g_health.adc_saturations != 0U);
  const bool ready_idle =
      (g_state == InstrumentState::kIdle) && !integrity_fault;

#if LOADCELL_LED_BLUE_CONFIGURED
  setIndicatorLed(LOADCELL_LED_BLUE_PIN, &g_blue_led_on, active);
#endif
#if LOADCELL_LED_RED_CONFIGURED
  setIndicatorLed(LOADCELL_LED_RED_PIN, &g_red_led_on, integrity_fault);
#endif
#if LOADCELL_LED_GREEN_CONFIGURED
  setIndicatorLed(LOADCELL_LED_GREEN_PIN, &g_green_led_on, ready_idle);
#endif
#endif
}

const char *stateName(InstrumentState state) {
  switch (state) {
    case InstrumentState::kUnavailable:
      return "UNAVAILABLE";
    case InstrumentState::kIdle:
      return "IDLE";
    case InstrumentState::kTaring:
      return "TARING";
    case InstrumentState::kRunning:
      return "RUNNING";
    case InstrumentState::kStopping:
      return "STOPPING";
  }
  return "UNKNOWN";
}

const char *availabilityName() {
#if !LOADCELL_PINS_CONFIGURED
  return "PINS_UNCONFIGURED";
#else
  return g_sensor_ready ? "READY" : "SENSOR_UNAVAILABLE";
#endif
}

const char *readyModeName() {
#if LOADCELL_DRDY_CONFIGURED
  return "PIN";
#else
  return "POLL";
#endif
}

bool isSupportedSampleRate(uint32_t sample_rate_sps) {
  return (sample_rate_sps == 10U) || (sample_rate_sps == 20U) ||
         (sample_rate_sps == 40U) || (sample_rate_sps == 80U) ||
         (sample_rate_sps == 320U);
}

bool isSupportedGain(uint32_t gain) {
  return (gain == 1U) || (gain == 2U) || (gain == 4U) || (gain == 8U) ||
         (gain == 16U) || (gain == 32U) || (gain == 64U) ||
         (gain == 128U);
}

[[maybe_unused]] uint64_t nominalSamplePeriodUs() {
  return 1000000ULL / static_cast<uint64_t>(g_sample_rate_sps);
}

void scheduleNextReadyPoll() {
#if LOADCELL_I2C_CONFIGURED && !LOADCELL_DRDY_CONFIGURED
  const uint64_t period_us = nominalSamplePeriodUs();
  uint64_t lead_us = (period_us / 20U) + kReadyPollIntervalUs;
  if (lead_us > (period_us / 2U)) {
    lead_us = period_us / 2U;
  }
  g_next_ready_poll_us = time_us_64() + period_us - lead_us;
#endif
}

[[maybe_unused]] uint8_t sampleRateRegisterValue(uint16_t sample_rate_sps) {
  switch (sample_rate_sps) {
    case 10:
      return NAU7802_SPS_10;
    case 20:
      return NAU7802_SPS_20;
    case 40:
      return NAU7802_SPS_40;
    case 80:
      return NAU7802_SPS_80;
    case 320:
      return NAU7802_SPS_320;
  }
  return NAU7802_SPS_320;
}

[[maybe_unused]] uint8_t gainRegisterValue(uint16_t gain) {
  switch (gain) {
    case 1:
      return NAU7802_GAIN_1;
    case 2:
      return NAU7802_GAIN_2;
    case 4:
      return NAU7802_GAIN_4;
    case 8:
      return NAU7802_GAIN_8;
    case 16:
      return NAU7802_GAIN_16;
    case 32:
      return NAU7802_GAIN_32;
    case 64:
      return NAU7802_GAIN_64;
    case 128:
      return NAU7802_GAIN_128;
  }
  return NAU7802_GAIN_128;
}

bool parseUnsigned(const char *text, uint32_t *value) {
  if ((text == nullptr) || (value == nullptr) || (*text == '\0') ||
      (*text == '-')) {
    return false;
  }

  char *end = nullptr;
  const unsigned long parsed = std::strtoul(text, &end, 10);
  if ((end == text) || (*end != '\0')) {
    return false;
  }
#if ULONG_MAX > UINT32_MAX
  if (parsed > UINT32_MAX) {
    return false;
  }
#endif
  *value = static_cast<uint32_t>(parsed);
  return true;
}

bool validIdentifier(const char *text) {
  if ((text == nullptr) || (*text == '\0') ||
      (std::strlen(text) >= sizeof(g_run.run_id))) {
    return false;
  }

  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    const char c = *cursor;
    const bool valid = ((c >= 'a') && (c <= 'z')) ||
                       ((c >= 'A') && (c <= 'Z')) ||
                       ((c >= '0') && (c <= '9')) || (c == '-') ||
                       (c == '_') || (c == '.');
    if (!valid) {
      return false;
    }
  }
  return true;
}

bool queueFormattedResponse(const char *format, ...) {
  if (g_response_queue.full()) {
    return false;
  }

  ProtocolLine line{};
  va_list args;
  va_start(args, format);
  const int result =
      std::vsnprintf(line.text, sizeof(line.text), format, args);
  va_end(args);
  if ((result < 0) || (static_cast<size_t>(result) >= sizeof(line.text))) {
    return false;
  }

  line.length = static_cast<size_t>(result);
  return g_response_queue.push(line);
}

void queueError(const char *command, const char *error_code,
                const char *description) {
  (void)queueFormattedResponse("ERR,%u,%s,%s,%s\n", kProtocolVersion,
                               command, error_code, description);
}

void recordReadyEvent(uint64_t timestamp_us) {
  g_last_drdy_timestamp_us = timestamp_us;
  if (!g_capture_edges) {
    return;
  }

  const uint32_t sequence = g_edge_sequence++;
  g_latest_captured_drdy_event.timestamp_us = timestamp_us;
  g_latest_captured_drdy_event.sequence = sequence;
  const uint8_t head = g_drdy_head;
  const uint8_t next =
      static_cast<uint8_t>((head + 1U) & (kDrdyQueueCapacity - 1U));
  if (next == g_drdy_tail) {
    ++g_drdy_overrun_count;
    return;
  }

  g_drdy_queue[head].timestamp_us = timestamp_us;
  g_drdy_queue[head].sequence = sequence;
  g_drdy_head = next;
}

[[maybe_unused]] void onDrdyRise() { recordReadyEvent(time_us_64()); }

bool takeNewestDrdyEvent(DrdyEvent *event, uint32_t *discarded_count) {
  if ((event == nullptr) || (discarded_count == nullptr)) {
    return false;
  }

  noInterrupts();
  uint8_t tail = g_drdy_tail;
  const uint8_t head = g_drdy_head;
  uint32_t queued_count = 0U;
  while (tail != head) {
    event->timestamp_us = g_drdy_queue[tail].timestamp_us;
    event->sequence = g_drdy_queue[tail].sequence;
    tail = static_cast<uint8_t>((tail + 1U) &
                                (kDrdyQueueCapacity - 1U));
    ++queued_count;
  }
  g_drdy_tail = tail;

  const uint32_t overrun_count = g_drdy_overrun_count;
  g_drdy_overrun_count = 0U;
  if (overrun_count > 0U) {
    // The newest captured conversion may be one which could not enter the
    // full queue. Pair the NAU7802's current output register with that newest
    // observed edge, never with an older retained timestamp.
    event->timestamp_us = g_latest_captured_drdy_event.timestamp_us;
    event->sequence = g_latest_captured_drdy_event.sequence;
  }
  interrupts();

  const uint32_t observed_count = queued_count + overrun_count;
  if (observed_count == 0U) {
    return false;
  }
  *discarded_count = observed_count - 1U;
  return true;
}

bool drdyQueueEmpty() {
  noInterrupts();
  const bool empty = g_drdy_tail == g_drdy_head;
  interrupts();
  return empty;
}

void resetDrdyQueueAndSequence() {
  noInterrupts();
  g_drdy_head = 0U;
  g_drdy_tail = 0U;
  g_drdy_overrun_count = 0U;
  g_edge_sequence = 0U;
  g_latest_captured_drdy_event.timestamp_us = 0U;
  g_latest_captured_drdy_event.sequence = 0U;
  g_retry_drdy_event_valid = false;
  g_next_ready_poll_us = 0U;
  interrupts();
}

uint64_t lastDrdyTimestamp() {
  noInterrupts();
  const uint64_t timestamp_us = g_last_drdy_timestamp_us;
  interrupts();
  return timestamp_us;
}

void setCaptureEdges(bool enabled) {
  noInterrupts();
  g_capture_edges = enabled;
  interrupts();
}

void primeCaptureIfAlreadyReady() {
#if LOADCELL_DRDY_CONFIGURED
  // When capture begins while DRDY is already high, there will be no new
  // rising edge until the unread result is consumed. Synthesize exactly one
  // event only when the ISR has not already queued that edge.
  noInterrupts();
  const bool need_event =
      g_capture_edges && (g_drdy_tail == g_drdy_head) &&
      (digitalRead(LOADCELL_DRDY_PIN) == HIGH);
  if (need_event) {
    onDrdyRise();
  }
  interrupts();
#elif LOADCELL_I2C_CONFIGURED
  // The first foreground acquisition pass will query PU_CTRL.CR immediately.
  g_next_ready_poll_us = 0U;
#endif
}

bool readRawCounts(int32_t *raw_counts, uint8_t *failed_attempts) {
#if LOADCELL_PINS_CONFIGURED
  if ((raw_counts == nullptr) || (failed_attempts == nullptr)) {
    return false;
  }
  *failed_attempts = 0U;

  for (uint8_t attempt = 0U; attempt < kI2cReadAttempts; ++attempt) {
    while (g_wire.available() > 0) {
      (void)g_wire.read();
    }

    g_wire.beginTransmission(kNau7802Address);
    g_wire.write(kNau7802AdcoB2Register);
    if (g_wire.endTransmission(false) != 0U) {
      ++(*failed_attempts);
      continue;
    }

    const size_t received = g_wire.requestFrom(
        static_cast<uint8_t>(kNau7802Address), static_cast<size_t>(3), true);
    if ((received != 3U) || (g_wire.available() < 3)) {
      while (g_wire.available() > 0) {
        (void)g_wire.read();
      }
      ++(*failed_attempts);
      continue;
    }

    const uint32_t raw = (static_cast<uint32_t>(g_wire.read()) << 16U) |
                         (static_cast<uint32_t>(g_wire.read()) << 8U) |
                         static_cast<uint32_t>(g_wire.read());
    *raw_counts = (raw & 0x00800000UL)
                      ? static_cast<int32_t>(raw | 0xFF000000UL)
                      : static_cast<int32_t>(raw);
    return true;
  }
  return false;
#else
  (void)raw_counts;
  (void)failed_attempts;
  return false;
#endif
}

[[maybe_unused]] bool readCycleReady(bool *ready) {
#if LOADCELL_I2C_CONFIGURED
  if (ready == nullptr) {
    return false;
  }
  while (g_wire.available() > 0) {
    (void)g_wire.read();
  }

  g_wire.beginTransmission(kNau7802Address);
  g_wire.write(kNau7802PuCtrlRegister);
  if (g_wire.endTransmission(false) != 0U) {
    return false;
  }

  const size_t received = g_wire.requestFrom(
      static_cast<uint8_t>(kNau7802Address), static_cast<size_t>(1), true);
  if ((received != 1U) || (g_wire.available() < 1)) {
    while (g_wire.available() > 0) {
      (void)g_wire.read();
    }
    return false;
  }

  *ready = (static_cast<uint8_t>(g_wire.read()) &
            kNau7802CycleReadyMask) != 0U;
  return true;
#else
  (void)ready;
  return false;
#endif
}

void accountI2cErrors(uint32_t count) {
  g_health.i2c_errors += count;
  if ((g_state == InstrumentState::kRunning) ||
      (g_state == InstrumentState::kStopping)) {
    g_run.i2c_error_count += count;
  }
}

void accountDroppedI2cSample() {
  ++g_health.dropped_samples;
  if ((g_state == InstrumentState::kRunning) ||
      (g_state == InstrumentState::kStopping)) {
    ++g_run.dropped_count;
  }
}

void accountDiscardedDrdyEvents(uint32_t count) {
  if (count == 0U) {
    return;
  }
  g_health.buffer_overruns += count;
  g_health.dropped_samples += count;
  if ((g_state == InstrumentState::kRunning) ||
      (g_state == InstrumentState::kStopping)) {
    g_run.buffer_overrun_count += count;
    g_run.dropped_count += count;
  }
}

void servicePolledReady() {
#if LOADCELL_I2C_CONFIGURED && !LOADCELL_DRDY_CONFIGURED
  if (!g_capture_edges || g_retry_drdy_event_valid || !drdyQueueEmpty()) {
    return;
  }

  const uint64_t now_us = time_us_64();
  if (now_us < g_next_ready_poll_us) {
    return;
  }
  g_next_ready_poll_us = now_us + kReadyPollIntervalUs;

  bool ready = false;
  if (!readCycleReady(&ready)) {
    accountI2cErrors(1U);
    return;
  }
  if (ready) {
    // This is the observation time, not a hardware edge time. Samples carry
    // kSampleFlagReadyPolled so downstream analysis retains that distinction.
    recordReadyEvent(time_us_64());
  }
#endif
}

bool serviceRunningSample(const DrdyEvent &event) {
  int32_t raw_counts = 0;
  uint8_t failed_attempts = 0U;
  if (!readRawCounts(&raw_counts, &failed_attempts)) {
    accountI2cErrors(failed_attempts);
    if (g_state == InstrumentState::kStopping) {
      accountDroppedI2cSample();
      return true;
    }
    return false;
  }
  accountI2cErrors(failed_attempts);
  scheduleNextReadyPoll();

  uint16_t flags = kSampleFlagNone;
#if !LOADCELL_DRDY_CONFIGURED
  flags |= kSampleFlagReadyPolled;
#endif
  if ((raw_counts == 0x007FFFFF) ||
      (raw_counts == static_cast<int32_t>(0xFF800000UL))) {
    flags |= kSampleFlagAdcSaturated;
    ++g_health.adc_saturations;
  }

  OutputRecord record{};
  record.type = OutputRecordType::kSample;
  record.data.sample.sequence = event.sequence;
  record.data.sample.timestamp_us = event.timestamp_us;
  record.data.sample.raw_counts = raw_counts;
  record.data.sample.flags = flags;
  record.data.sample.dropped_total = g_run.dropped_count;

  if (!g_output_queue.push(record)) {
    ++g_health.buffer_overruns;
    ++g_health.dropped_samples;
    ++g_run.buffer_overrun_count;
    ++g_run.dropped_count;
    return true;
  }

  if (!g_run.has_sample) {
    g_run.has_sample = true;
    g_run.first_sequence = event.sequence;
    g_run.first_timestamp_us = event.timestamp_us;
  }
  g_run.last_sequence = event.sequence;
  g_run.last_timestamp_us = event.timestamp_us;
  ++g_run.captured_count;
  return true;
}

void finishTare() {
  setCaptureEdges(false);
  resetDrdyQueueAndSequence();
  g_state = InstrumentState::kIdle;
  g_last_tare_mean = g_tare.mean;
  g_last_tare_count = g_tare.captured_count;
  g_last_tare_stddev =
      (g_tare.captured_count > 0U)
          ? std::sqrt(g_tare.m2 /
                      static_cast<double>(g_tare.captured_count))
          : 0.0;
  (void)queueFormattedResponse("OK,%u,TARE,COMPLETE,%lu,%.3f,%.3f\n",
                               kProtocolVersion,
                               static_cast<unsigned long>(g_last_tare_count),
                               g_last_tare_mean, g_last_tare_stddev);
}

bool serviceTareSample(const DrdyEvent &) {
  int32_t raw_counts = 0;
  uint8_t failed_attempts = 0U;
  if (!readRawCounts(&raw_counts, &failed_attempts)) {
    accountI2cErrors(failed_attempts);
    return false;
  }
  accountI2cErrors(failed_attempts);
  scheduleNextReadyPoll();

  if ((raw_counts == 0x007FFFFF) ||
      (raw_counts == static_cast<int32_t>(0xFF800000UL))) {
    ++g_health.adc_saturations;
    setCaptureEdges(false);
    resetDrdyQueueAndSequence();
    g_state = InstrumentState::kIdle;
    queueError("TARE", "ADC_SATURATED", "tare_raw_counts_at_24bit_limit");
    return true;
  }

  ++g_tare.captured_count;
  const double value = static_cast<double>(raw_counts);
  const double delta = value - g_tare.mean;
  g_tare.mean += delta / static_cast<double>(g_tare.captured_count);
  const double delta2 = value - g_tare.mean;
  g_tare.m2 += delta * delta2;

  if (g_tare.captured_count >= g_tare.requested_count) {
    finishTare();
  }
  return true;
}

void serviceAcquisition() {
  servicePolledReady();

  DrdyEvent newest_event{};
  uint32_t discarded_count = 0U;
  bool have_event = false;
  if (g_retry_drdy_event_valid) {
    newest_event = g_retry_drdy_event;
    have_event = true;
  } else if (takeNewestDrdyEvent(&newest_event, &discarded_count)) {
    have_event = true;
    accountDiscardedDrdyEvents(discarded_count);
  }

  if (have_event) {
    bool consumed = true;
    if ((g_state == InstrumentState::kRunning) ||
        (g_state == InstrumentState::kStopping)) {
      consumed = serviceRunningSample(newest_event);
    } else if (g_state == InstrumentState::kTaring) {
      consumed = serviceTareSample(newest_event);
    }
    if (consumed) {
      g_retry_drdy_event_valid = false;
    } else {
      g_retry_drdy_event = newest_event;
      g_retry_drdy_event_valid = true;
    }
  }

  if ((g_state == InstrumentState::kTaring) &&
      (time_us_64() >= g_tare.deadline_us)) {
    setCaptureEdges(false);
    resetDrdyQueueAndSequence();
    g_state = InstrumentState::kIdle;
    queueError("TARE", "TIMEOUT", "tare_sample_deadline_expired");
  }
}

void serviceIdleReady() {
#if LOADCELL_I2C_CONFIGURED
  if ((g_state != InstrumentState::kIdle) || !g_sensor_ready) {
    return;
  }

#if LOADCELL_DRDY_CONFIGURED
  if (digitalRead(LOADCELL_DRDY_PIN) == LOW) {
    return;
  }
#else
  const uint64_t now_us = time_us_64();
  if (now_us < g_next_ready_poll_us) {
    return;
  }
  g_next_ready_poll_us = now_us + kReadyPollIntervalUs;
  bool ready = false;
  if (!readCycleReady(&ready)) {
    accountI2cErrors(1U);
    return;
  }
  if (!ready) {
    return;
  }
  g_last_drdy_timestamp_us = time_us_64();
#endif

  int32_t ignored = 0;
  uint8_t failed_attempts = 0U;
  const bool read_ok = readRawCounts(&ignored, &failed_attempts);
  accountI2cErrors(failed_attempts);
  if (read_ok) {
    scheduleNextReadyPoll();
  }
#endif
}

bool configureScale(uint16_t sample_rate_sps, uint16_t gain) {
#if LOADCELL_PINS_CONFIGURED
  const bool rate_ok =
      g_scale.setSampleRate(sampleRateRegisterValue(sample_rate_sps));
  const bool gain_ok = g_scale.setGain(gainRegisterValue(gain));
  const bool calibration_ok = rate_ok && gain_ok && g_scale.calibrateAFE();
  if (!calibration_ok) {
    return false;
  }
  g_sample_rate_sps = sample_rate_sps;
  g_gain = gain;
  g_next_ready_poll_us = 0U;
  return true;
#else
  (void)sample_rate_sps;
  (void)gain;
  return false;
#endif
}

void resetRun(const char *run_id) {
  std::memset(&g_run, 0, sizeof(g_run));
  std::strncpy(g_run.run_id, run_id, sizeof(g_run.run_id) - 1U);
  g_final_enqueued = false;
}

void handleInfo() {
  (void)queueFormattedResponse(
      "OK,%u,INFO,%s,%s,pins_configured=%u,i2c_configured=%u,"
      "drdy_configured=%u,sensor_ready=%u,sda=%d,scl=%d,drdy=%d,"
      "i2c_instance=%u,ready_mode=%s,ready_poll_interval_us=%llu,"
      "leds_configured=%u,led_active_high=%u,led_blue=%d,led_red=%d,"
      "led_green=%d,availability=%s,sample_rate_sps=%u,gain=%u,"
      "output_capacity=%u\n",
      kProtocolVersion, kFirmwareName, kFirmwareVersion,
      static_cast<unsigned>(LOADCELL_PINS_CONFIGURED),
      static_cast<unsigned>(LOADCELL_I2C_CONFIGURED),
      static_cast<unsigned>(LOADCELL_DRDY_CONFIGURED),
      static_cast<unsigned>(g_sensor_ready), LOADCELL_SDA_PIN,
      LOADCELL_SCL_PIN, LOADCELL_DRDY_PIN,
      static_cast<unsigned>(LOADCELL_I2C_INSTANCE), readyModeName(),
      static_cast<unsigned long long>(LOADCELL_DRDY_CONFIGURED
                                           ? 0ULL
                                           : kReadyPollIntervalUs),
      static_cast<unsigned>(LOADCELL_LEDS_CONFIGURED),
      static_cast<unsigned>(LOADCELL_LED_ACTIVE_HIGH),
      LOADCELL_LED_BLUE_PIN, LOADCELL_LED_RED_PIN, LOADCELL_LED_GREEN_PIN,
      availabilityName(),
      static_cast<unsigned>(g_sample_rate_sps),
      static_cast<unsigned>(g_gain),
      static_cast<unsigned>(kOutputQueueCapacity));
}

void handleStatus() {
  const uint64_t now_us = time_us_64();
  const uint64_t last_drdy_us = lastDrdyTimestamp();
  const uint64_t drdy_age_us =
      (last_drdy_us == 0U) ? UINT64_MAX : (now_us - last_drdy_us);
  if (drdy_age_us == UINT64_MAX) {
    (void)queueFormattedResponse(
        "OK,%u,STATUS,%s,availability=%s,sensor_ready=%u,"
        "sample_rate_sps=%u,gain=%u,"
        "sequence=%lu,captured=%lu,dropped=%lu,i2c_errors=%lu,"
        "buffer_overruns=%lu,saturations=%lu,drdy_age_us=NA,queued=%u\n",
        kProtocolVersion, stateName(g_state), availabilityName(),
        static_cast<unsigned>(g_sensor_ready),
        static_cast<unsigned>(g_sample_rate_sps),
        static_cast<unsigned>(g_gain),
        static_cast<unsigned long>(g_edge_sequence),
        static_cast<unsigned long>(g_run.captured_count),
        static_cast<unsigned long>(g_health.dropped_samples),
        static_cast<unsigned long>(g_health.i2c_errors),
        static_cast<unsigned long>(g_health.buffer_overruns),
        static_cast<unsigned long>(g_health.adc_saturations),
        static_cast<unsigned>(g_output_queue.size()));
  } else {
    (void)queueFormattedResponse(
        "OK,%u,STATUS,%s,availability=%s,sensor_ready=%u,"
        "sample_rate_sps=%u,gain=%u,"
        "sequence=%lu,captured=%lu,dropped=%lu,i2c_errors=%lu,"
        "buffer_overruns=%lu,saturations=%lu,drdy_age_us=%llu,queued=%u\n",
        kProtocolVersion, stateName(g_state), availabilityName(),
        static_cast<unsigned>(g_sensor_ready),
        static_cast<unsigned>(g_sample_rate_sps),
        static_cast<unsigned>(g_gain),
        static_cast<unsigned long>(g_edge_sequence),
        static_cast<unsigned long>(g_run.captured_count),
        static_cast<unsigned long>(g_health.dropped_samples),
        static_cast<unsigned long>(g_health.i2c_errors),
        static_cast<unsigned long>(g_health.buffer_overruns),
        static_cast<unsigned long>(g_health.adc_saturations),
        static_cast<unsigned long long>(drdy_age_us),
        static_cast<unsigned>(g_output_queue.size()));
  }
}

bool requireUsableSensor(const char *command) {
#if !LOADCELL_PINS_CONFIGURED
  queueError(command, "PINS_UNCONFIGURED",
             "compile_time_pin_assignment_required");
  return false;
#else
  if (!g_sensor_ready) {
    queueError(command, "SENSOR_UNAVAILABLE", "nau7802_not_ready");
    return false;
  }
  return true;
#endif
}

void handleConfig(char *rate_token, char *gain_token, char *extra_token) {
  if ((rate_token == nullptr) || (gain_token == nullptr) ||
      (extra_token != nullptr)) {
    queueError("CONFIG", "MALFORMED", "expected_CONFIG_sps_gain");
    return;
  }
  if (g_state != InstrumentState::kIdle) {
    queueError("CONFIG", "NOT_IDLE", "configuration_is_idle_only");
    return;
  }
  if (!requireUsableSensor("CONFIG")) {
    return;
  }

  uint32_t sample_rate_sps = 0U;
  uint32_t gain = 0U;
  if (!parseUnsigned(rate_token, &sample_rate_sps) ||
      !isSupportedSampleRate(sample_rate_sps)) {
    queueError("CONFIG", "BAD_SAMPLE_RATE", "allowed_10_20_40_80_320");
    return;
  }
  if (!parseUnsigned(gain_token, &gain) || !isSupportedGain(gain)) {
    queueError("CONFIG", "BAD_GAIN", "allowed_1_2_4_8_16_32_64_128");
    return;
  }

  if (!configureScale(static_cast<uint16_t>(sample_rate_sps),
                      static_cast<uint16_t>(gain))) {
    g_sensor_ready = false;
    g_state = InstrumentState::kUnavailable;
    queueError("CONFIG", "CALIBRATION_FAILED", "nau7802_AFE_not_ready");
    return;
  }
  (void)queueFormattedResponse("OK,%u,CONFIG,%lu,%lu\n", kProtocolVersion,
                               static_cast<unsigned long>(sample_rate_sps),
                               static_cast<unsigned long>(gain));
}

void handleTare(char *count_token, char *extra_token) {
  if ((count_token == nullptr) || (extra_token != nullptr)) {
    queueError("TARE", "MALFORMED", "expected_TARE_sample_count");
    return;
  }
  if (g_state != InstrumentState::kIdle) {
    queueError("TARE", "NOT_IDLE", "tare_is_idle_only");
    return;
  }
  if (!requireUsableSensor("TARE")) {
    return;
  }

  uint32_t sample_count = 0U;
  if (!parseUnsigned(count_token, &sample_count) || (sample_count == 0U) ||
      (sample_count > kMaxTareSamples)) {
    queueError("TARE", "BAD_SAMPLE_COUNT", "allowed_1_through_1024");
    return;
  }

  g_tare = {};
  g_tare.requested_count = sample_count;
  const uint64_t expected_us =
      (static_cast<uint64_t>(sample_count) * 1000000ULL) /
      static_cast<uint64_t>(g_sample_rate_sps);
  uint64_t timeout_us = expected_us + 2000000ULL;
  if (timeout_us > kTareTimeoutCeilingUs) {
    timeout_us = kTareTimeoutCeilingUs;
  }
  g_tare.deadline_us = time_us_64() + timeout_us;

  resetDrdyQueueAndSequence();
  g_state = InstrumentState::kTaring;
  setCaptureEdges(true);
  primeCaptureIfAlreadyReady();
  (void)queueFormattedResponse("OK,%u,TARE,STARTED,%lu\n", kProtocolVersion,
                               static_cast<unsigned long>(sample_count));
}

void handleStart(char *run_id, char *extra_token) {
  if ((run_id == nullptr) || (extra_token != nullptr) ||
      !validIdentifier(run_id)) {
    queueError("START", "BAD_RUN_ID", "use_1_to_31_safe_identifier_chars");
    return;
  }
  if (g_state != InstrumentState::kIdle) {
    queueError("START", "NOT_IDLE", "capture_already_active");
    return;
  }
  if (!requireUsableSensor("START")) {
    return;
  }

  g_output_queue.clear();
  resetRun(run_id);
  resetDrdyQueueAndSequence();
  g_state = InstrumentState::kRunning;
  const uint64_t start_timestamp_us = time_us_64();
  setCaptureEdges(true);
  primeCaptureIfAlreadyReady();
  (void)queueFormattedResponse("OK,%u,START,%s,%llu\n", kProtocolVersion,
                               g_run.run_id,
                               static_cast<unsigned long long>(start_timestamp_us));
}

void handleMark(char *marker_id, char *extra_token) {
  if ((marker_id == nullptr) || (extra_token != nullptr) ||
      !validIdentifier(marker_id)) {
    queueError("MARK", "BAD_MARKER_ID",
               "use_1_to_31_safe_identifier_chars");
    return;
  }
  if (g_state != InstrumentState::kRunning) {
    queueError("MARK", "NOT_RUNNING", "marker_requires_active_capture");
    return;
  }

  // Establish marker order against conversion-ready events already observed
  // by polling or timestamped by the ISR. Retry until no earlier sample is
  // pending.
  for (;;) {
    noInterrupts();
    const bool pending_sample = g_drdy_tail != g_drdy_head;
    if (!pending_sample) {
      break;
    }
    interrupts();
    serviceAcquisition();
    if (g_state != InstrumentState::kRunning) {
      queueError("MARK", "NOT_RUNNING", "capture_ended_before_marker");
      return;
    }
  }

  OutputRecord record{};
  record.type = OutputRecordType::kMarker;
  record.data.marker.timestamp_us = time_us_64();
  std::strncpy(record.data.marker.marker_id, marker_id,
               sizeof(record.data.marker.marker_id) - 1U);
  if (!g_output_queue.push(record)) {
    interrupts();
    ++g_health.buffer_overruns;
    ++g_run.buffer_overrun_count;
    queueError("MARK", "BUFFER_FULL", "marker_not_recorded");
    return;
  }
  interrupts();
  (void)queueFormattedResponse("OK,%u,MARK,%s,%llu\n", kProtocolVersion,
                               marker_id,
                               static_cast<unsigned long long>(
                                   record.data.marker.timestamp_us));
}

void handleStop(char *extra_token) {
  if (extra_token != nullptr) {
    queueError("STOP", "MALFORMED", "STOP_takes_no_arguments");
    return;
  }
  if (g_state != InstrumentState::kRunning) {
    queueError("STOP", "NOT_RUNNING", "no_active_capture");
    return;
  }

  noInterrupts();
  g_capture_edges = false;
  const uint64_t stop_timestamp_us = time_us_64();
  interrupts();
  g_state = InstrumentState::kStopping;
  (void)queueFormattedResponse("OK,%u,STOP,DRAINING,%llu\n",
                               kProtocolVersion,
                               static_cast<unsigned long long>(
                                   stop_timestamp_us));
}

void handleCommand(char *line) {
  char *save = nullptr;
  char *command = strtok_r(line, " \t", &save);
  if (command == nullptr) {
    return;
  }
  char *arg1 = strtok_r(nullptr, " \t", &save);
  char *arg2 = strtok_r(nullptr, " \t", &save);
  char *arg3 = strtok_r(nullptr, " \t", &save);

  if (std::strcmp(command, "INFO") == 0) {
    if (arg1 != nullptr) {
      queueError("INFO", "MALFORMED", "INFO_takes_no_arguments");
    } else {
      handleInfo();
    }
  } else if (std::strcmp(command, "STATUS") == 0) {
    if (arg1 != nullptr) {
      queueError("STATUS", "MALFORMED", "STATUS_takes_no_arguments");
    } else {
      handleStatus();
    }
  } else if (std::strcmp(command, "CONFIG") == 0) {
    handleConfig(arg1, arg2, arg3);
  } else if (std::strcmp(command, "TARE") == 0) {
    handleTare(arg1, arg2);
  } else if (std::strcmp(command, "START") == 0) {
    handleStart(arg1, arg2);
  } else if (std::strcmp(command, "MARK") == 0) {
    handleMark(arg1, arg2);
  } else if (std::strcmp(command, "STOP") == 0) {
    handleStop(arg1);
  } else {
    queueError("UNKNOWN", "UNKNOWN_COMMAND", "unsupported_command");
  }
}

void serviceCommands() {
  // Leave one response slot free before consuming another line. This makes a
  // slow host apply USB RX backpressure instead of losing command responses.
  if (g_response_queue.size() >= (kResponseQueueCapacity - 1U)) {
    return;
  }

  constexpr size_t kMaximumRxBytesPerPass = 64U;
  for (size_t count = 0U;
       (count < kMaximumRxBytesPerPass) && (Serial.available() > 0); ++count) {
    const int incoming = Serial.read();
    if (incoming < 0) {
      break;
    }
    const char character = static_cast<char>(incoming);
    if (character == '\r') {
      continue;
    }
    if (character == '\n') {
      if (g_discarding_long_command) {
        queueError("LINE", "TOO_LONG", "command_exceeds_127_bytes");
      } else if (g_command_length > 0U) {
        g_command_line[g_command_length] = '\0';
        handleCommand(g_command_line);
      }
      g_command_length = 0U;
      g_discarding_long_command = false;
      if (g_response_queue.size() >= (kResponseQueueCapacity - 1U)) {
        break;
      }
      continue;
    }

    if (g_discarding_long_command) {
      continue;
    }
    if (g_command_length >= (sizeof(g_command_line) - 1U)) {
      g_command_length = 0U;
      g_discarding_long_command = true;
      continue;
    }
    g_command_line[g_command_length++] = character;
  }
}

bool formatOutputRecord(const OutputRecord &record, ProtocolLine *line) {
  if (line == nullptr) {
    return false;
  }

  int result = -1;
  switch (record.type) {
    case OutputRecordType::kSample:
      result = std::snprintf(
          line->text, sizeof(line->text), "S,%u,%lu,%llu,%ld,%04X,%lu\n",
          kProtocolVersion,
          static_cast<unsigned long>(record.data.sample.sequence),
          static_cast<unsigned long long>(record.data.sample.timestamp_us),
          static_cast<long>(record.data.sample.raw_counts),
          static_cast<unsigned>(record.data.sample.flags),
          static_cast<unsigned long>(record.data.sample.dropped_total));
      break;
    case OutputRecordType::kMarker:
      result = std::snprintf(
          line->text, sizeof(line->text), "M,%u,%s,%llu\n", kProtocolVersion,
          record.data.marker.marker_id,
          static_cast<unsigned long long>(record.data.marker.timestamp_us));
      break;
    case OutputRecordType::kFinal: {
      const uint64_t timestamp_span_us =
          (record.data.final.captured_count > 1U)
              ? (record.data.final.last_timestamp_us -
                 record.data.final.first_timestamp_us)
              : 0U;
      const uint64_t average_millisps =
          (timestamp_span_us > 0U)
              ? ((static_cast<uint64_t>(record.data.final.captured_count - 1U) *
                  1000000000ULL) /
                 timestamp_span_us)
              : 0U;
      result = std::snprintf(
          line->text, sizeof(line->text),
          "F,%u,%s,%lu,%lu,%lu,%lu,%lu,%lu,%llu,%llu,%llu.%03llu\n",
          kProtocolVersion, record.data.final.run_id,
          static_cast<unsigned long>(record.data.final.first_sequence),
          static_cast<unsigned long>(record.data.final.last_sequence),
          static_cast<unsigned long>(record.data.final.captured_count),
          static_cast<unsigned long>(record.data.final.dropped_count),
          static_cast<unsigned long>(record.data.final.i2c_error_count),
          static_cast<unsigned long>(record.data.final.buffer_overrun_count),
          static_cast<unsigned long long>(record.data.final.first_timestamp_us),
          static_cast<unsigned long long>(record.data.final.last_timestamp_us),
          static_cast<unsigned long long>(average_millisps / 1000ULL),
          static_cast<unsigned long long>(average_millisps % 1000ULL));
      break;
    }
  }

  if ((result < 0) || (static_cast<size_t>(result) >= sizeof(line->text))) {
    return false;
  }
  line->length = static_cast<size_t>(result);
  return true;
}

void enqueueFinalWhenAcquisitionDrained() {
  if ((g_state != InstrumentState::kStopping) || g_final_enqueued ||
      !drdyQueueEmpty() || g_output_queue.full()) {
    return;
  }

  OutputRecord record{};
  record.type = OutputRecordType::kFinal;
  std::strncpy(record.data.final.run_id, g_run.run_id,
               sizeof(record.data.final.run_id) - 1U);
  record.data.final.first_sequence =
      g_run.has_sample ? g_run.first_sequence : 0U;
  record.data.final.last_sequence =
      g_run.has_sample ? g_run.last_sequence : 0U;
  record.data.final.captured_count = g_run.captured_count;
  record.data.final.dropped_count = g_run.dropped_count;
  record.data.final.i2c_error_count = g_run.i2c_error_count;
  record.data.final.buffer_overrun_count = g_run.buffer_overrun_count;
  record.data.final.first_timestamp_us =
      g_run.has_sample ? g_run.first_timestamp_us : 0U;
  record.data.final.last_timestamp_us =
      g_run.has_sample ? g_run.last_timestamp_us : 0U;
  if (g_output_queue.push(record)) {
    g_final_enqueued = true;
  }
}

void loadNextTxLine() {
  if (g_active_tx_valid) {
    return;
  }

  // Command acknowledgements are small and take priority. Output records keep
  // their own strict sample/marker/final ordering in the separate ring.
  if (g_response_queue.pop(&g_active_tx_line)) {
    g_active_tx_offset = 0U;
    g_active_tx_valid = true;
    g_active_tx_is_final = false;
    return;
  }

  OutputRecord record{};
  if (!g_output_queue.peek(&record)) {
    return;
  }
  if (!formatOutputRecord(record, &g_active_tx_line)) {
    (void)g_output_queue.pop(&record);
    return;
  }
  (void)g_output_queue.pop(&record);
  g_active_tx_offset = 0U;
  g_active_tx_valid = true;
  g_active_tx_is_final = record.type == OutputRecordType::kFinal;
}

void serviceTx() {
  const bool usb_connected = static_cast<bool>(Serial);
  if (!usb_connected) {
    if (g_usb_was_connected && g_active_tx_valid) {
      // The disconnected host may have received only a prefix. Preserve the
      // record, but start it at a line boundary for the next CDC session.
      g_active_tx_offset = 0U;
    }
    g_usb_was_connected = false;
    return;
  }
  if (!g_usb_was_connected && g_active_tx_valid) {
    g_active_tx_offset = 0U;
  }
  g_usb_was_connected = true;
  loadNextTxLine();
  if (!g_active_tx_valid) {
    return;
  }

  const int write_capacity = Serial.availableForWrite();
  if (write_capacity <= 0) {
    return;
  }
  const size_t remaining = g_active_tx_line.length - g_active_tx_offset;
  size_t chunk = static_cast<size_t>(write_capacity);
  if (chunk > remaining) {
    chunk = remaining;
  }
  if (chunk > kMaxTxChunk) {
    chunk = kMaxTxChunk;
  }
  const size_t written = Serial.write(
      reinterpret_cast<const uint8_t *>(g_active_tx_line.text) +
          g_active_tx_offset,
      chunk);
  g_active_tx_offset += written;
  if (g_active_tx_offset < g_active_tx_line.length) {
    return;
  }

  g_active_tx_valid = false;
  g_active_tx_offset = 0U;
  if (g_active_tx_is_final) {
    g_active_tx_is_final = false;
    g_final_enqueued = false;
    g_state = InstrumentState::kIdle;
    std::memset(&g_run, 0, sizeof(g_run));
  }
}

bool initializeSensor() {
#if LOADCELL_I2C_CONFIGURED
#if LOADCELL_DRDY_CONFIGURED
  pinMode(LOADCELL_DRDY_PIN, INPUT);
#endif
  if (!g_wire.setSDA(LOADCELL_SDA_PIN) ||
      !g_wire.setSCL(LOADCELL_SCL_PIN)) {
    return false;
  }
  g_wire.begin();
  g_wire.setClock(LOADCELL_I2C_HZ);
  g_wire.setTimeout(5U, true);

  if (!g_scale.begin(g_wire)) {
    return false;
  }
#if LOADCELL_DRDY_CONFIGURED
  if (!g_scale.setIntPolarityHigh()) {
    return false;
  }
#endif
  if (!configureScale(LOADCELL_DEFAULT_SAMPLE_RATE_SPS,
                      LOADCELL_DEFAULT_GAIN)) {
    return false;
  }

#if LOADCELL_DRDY_CONFIGURED
  attachInterrupt(digitalPinToInterrupt(LOADCELL_DRDY_PIN), onDrdyRise,
                  RISING);
#else
  g_next_ready_poll_us = 0U;
#endif
  return true;
#else
  return false;
#endif
}

}  // namespace
}  // namespace loadcell

void setup() {
  loadcell::initializeIndicatorLeds();
  Serial.begin(115200);
  // Native USB must never gate acquisition or boot on a host opening CDC.

  loadcell::g_sensor_ready = loadcell::initializeSensor();
  loadcell::g_state = loadcell::g_sensor_ready
                          ? loadcell::InstrumentState::kIdle
                          : loadcell::InstrumentState::kUnavailable;
  loadcell::updateIndicatorLeds();
}

void loop() {
  loadcell::serviceAcquisition();
  loadcell::enqueueFinalWhenAcquisitionDrained();
  loadcell::serviceCommands();
  loadcell::serviceTx();
  loadcell::serviceIdleReady();
  loadcell::updateIndicatorLeds();
}
