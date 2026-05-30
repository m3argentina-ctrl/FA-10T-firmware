#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t acs712_init(void);

// Sample the AC waveform for ~100ms and return I_rms in amps.
esp_err_t acs712_read_rms(float *amps_out);

// Average several RMS samples while fans are ON to learn nominal. Blocks for
// approximately ACS712_LEARN_DURATION_MS. Caller must have fans energised.
esp_err_t acs712_learn_nominal(float *nominal_out);

#ifdef __cplusplus
}
#endif
