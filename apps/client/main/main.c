#include <stdint.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lnot_device_role.h"
#include "sdkconfig.h"

static const char *const TAG = "lnot_client";

static void heartbeat_callback(void *argument)
{
    (void)argument;
    ESP_LOGI(TAG, "%s firmware %s is running", lnot_device_role_name(LNOT_DEVICE_ROLE_CLIENT),
             esp_app_get_description()->version);
}

void app_main(void)
{
    const esp_timer_create_args_t heartbeat_timer_args = {
        .callback = heartbeat_callback,
        .name = "lnot_heartbeat",
    };
    esp_timer_handle_t heartbeat_timer;

    ESP_LOGI(TAG, "Starting %s firmware %s", lnot_device_role_name(LNOT_DEVICE_ROLE_CLIENT),
             esp_app_get_description()->version);
    ESP_ERROR_CHECK(esp_timer_create(&heartbeat_timer_args, &heartbeat_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(
        heartbeat_timer, (uint64_t)CONFIG_LNOT_HEARTBEAT_INTERVAL_SECONDS * 1000000ULL));
}
