#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool lnot_wifi_device_name_from_mac(const uint8_t mac[6], char *device_name, size_t capacity);

#ifdef __cplusplus
}
#endif
