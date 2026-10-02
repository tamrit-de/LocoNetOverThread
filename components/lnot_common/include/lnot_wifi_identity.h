#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool lnot_wifi_ap_ssid_from_mac(const uint8_t mac[6], char *ssid, size_t capacity);

#ifdef __cplusplus
}
#endif
