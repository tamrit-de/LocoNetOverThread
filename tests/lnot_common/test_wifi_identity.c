#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "lnot_wifi_identity.h"

int main(void)
{
    const uint8_t mac[6] = {0x10, 0x20, 0x30, 0xAB, 0xCD, 0xEF};
    char ssid[17];
    assert(lnot_wifi_ap_ssid_from_mac(mac, ssid, sizeof(ssid)));
    assert(strcmp(ssid, "LocoNet-30ABCDEF") == 0);
    assert(!lnot_wifi_ap_ssid_from_mac(mac, ssid, sizeof(ssid) - 1));
    assert(!lnot_wifi_ap_ssid_from_mac(NULL, ssid, sizeof(ssid)));
    assert(!lnot_wifi_ap_ssid_from_mac(mac, NULL, sizeof(ssid)));
    return 0;
}
