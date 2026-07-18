#pragma once

#include <stdbool.h>
#include "esp_err.h"

/** Build and publish a DSP profile for the currently claimed USB transport. */
esp_err_t mvs_device_runtime_identify(void);

/** Clear all effect IDs and capabilities after disconnect/device change. */
void mvs_device_runtime_clear(void);

/** True only after transport identification and profile publication completed. */
bool mvs_device_runtime_is_ready(void);
