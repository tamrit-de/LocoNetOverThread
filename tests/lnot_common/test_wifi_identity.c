#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "lnot_wifi_identity.h"

int main(void)
{
    const uint8_t mac[6] = {0x10, 0x20, 0x30, 0xAB, 0xCD, 0xEF};
    char device_name[17];
    assert(lnot_wifi_device_name_from_mac(mac, device_name, sizeof(device_name)));
    assert(strcmp(device_name, "LocoNet-30ABCDEF") == 0);
    assert(!lnot_wifi_device_name_from_mac(mac, device_name, sizeof(device_name) - 1));
    assert(!lnot_wifi_device_name_from_mac(NULL, device_name, sizeof(device_name)));
    assert(!lnot_wifi_device_name_from_mac(mac, NULL, sizeof(device_name)));
    return 0;
}
