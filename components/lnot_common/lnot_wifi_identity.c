#include <stdio.h>

#include "lnot_wifi_identity.h"

bool lnot_wifi_device_name_from_mac(const uint8_t mac[6], char *device_name, size_t capacity)
{
    if (mac == NULL || device_name == NULL || capacity < 17) {
        return false;
    }
    int length = snprintf(
        device_name, capacity, "LocoNet-%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    return length == 16;
}
