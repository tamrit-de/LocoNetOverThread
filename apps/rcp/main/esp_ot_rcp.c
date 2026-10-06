/*
 * Derived from the ESP-IDF v5.3.2 OpenThread RCP example, which is
 * CC0-1.0 licensed.
 */

#include "esp_event.h"
#include "esp_openthread.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"

#include "esp_ot_config.h"

#if !SOC_IEEE802154_SUPPORTED
#error "RCP is only supported for SoCs with an IEEE 802.15.4 module"
#endif

extern void otAppNcpInit(otInstance *instance);

static void ot_task_worker(void *context)
{
    (void)context;
    const esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_openthread_init(&config));
    otAppNcpInit(esp_openthread_get_instance());
    esp_openthread_launch_mainloop();
    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

void app_main(void)
{
    const esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 2,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    xTaskCreate(ot_task_worker, "ot_rcp_main", 3072, NULL, 5, NULL);
}
