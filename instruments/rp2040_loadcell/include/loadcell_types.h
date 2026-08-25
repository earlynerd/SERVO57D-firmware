#pragma once

#include <stddef.h>
#include <stdint.h>

namespace loadcell {

enum class InstrumentState : uint8_t {
  kUnavailable,
  kIdle,
  kTaring,
  kRunning,
  kStopping,
};

enum SampleFlag : uint16_t {
  kSampleFlagNone = 0,
  kSampleFlagAdcSaturated = 1U << 0,
  kSampleFlagReadyPolled = 1U << 1,
};

struct DrdyEvent {
  uint64_t timestamp_us;
  uint32_t sequence;
};

struct SampleRecord {
  uint32_t sequence;
  uint64_t timestamp_us;
  int32_t raw_counts;
  uint32_t dropped_total;
  uint16_t flags;
};

struct MarkerRecord {
  char marker_id[32];
  uint64_t timestamp_us;
};

struct FinalRecord {
  char run_id[32];
  uint32_t first_sequence;
  uint32_t last_sequence;
  uint32_t captured_count;
  uint32_t dropped_count;
  uint32_t i2c_error_count;
  uint32_t buffer_overrun_count;
  uint64_t first_timestamp_us;
  uint64_t last_timestamp_us;
};

enum class OutputRecordType : uint8_t {
  kSample,
  kMarker,
  kFinal,
};

struct OutputRecord {
  OutputRecordType type;
  union {
    SampleRecord sample;
    MarkerRecord marker;
    FinalRecord final;
  } data;
};

struct HealthCounters {
  uint32_t dropped_samples;
  uint32_t i2c_errors;
  uint32_t buffer_overruns;
  uint32_t adc_saturations;
};

struct RunCounters {
  char run_id[32];
  uint32_t captured_count;
  uint32_t dropped_count;
  uint32_t i2c_error_count;
  uint32_t buffer_overrun_count;
  uint32_t first_sequence;
  uint32_t last_sequence;
  uint64_t first_timestamp_us;
  uint64_t last_timestamp_us;
  bool has_sample;
};

struct TareAccumulator {
  uint32_t requested_count;
  uint32_t captured_count;
  uint64_t deadline_us;
  double mean;
  double m2;
};

}  // namespace loadcell
