#include <stdio.h>

#include "lnot_wifi_identity.h"

bool lnot_wifi_ap_ssid_from_mac(const uint8_t mac[6], char *ssid, size_t capacity)
{
    if (mac == NULL || ssid == NULL || capacity < 17) {
        return false;
    }
    int length = snprintf(
        ssid, capacity, "LocoNet-%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    return length == 16;
}
