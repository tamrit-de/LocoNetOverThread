/*
 * Derived from the ESP-IDF v5.3.2 OpenThread RCP example, which is
 * CC0-1.0 licensed.
 */

#pragma once

#include "esp_openthread_types.h"

#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() \
    {                                          \
        .radio_mode = RADIO_MODE_NATIVE,       \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()      \
    {                                             \
        .host_connection_mode = HOST_CONNECTION_MODE_RCP_SPI, \
        .spi_slave_config = {                     \
            .host_device = SPI2_HOST,             \
            .bus_config = {                       \
                .mosi_io_num = 3,                 \
                .miso_io_num = 1,                 \
                .sclk_io_num = 0,                 \
                .quadhd_io_num = -1,              \
                .quadwp_io_num = -1,              \
                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO, \
            },                                    \
            .slave_config = {                     \
                .mode = 0,                        \
                .spics_io_num = 2,                \
                .queue_size = 3,                  \
                .flags = 0,                       \
            },                                    \
            .intr_pin = 9,                        \
        },                                        \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG() \
    {                                         \
        .storage_partition_name = "nvs",      \
        .netif_queue_size = 10,               \
        .task_queue_size = 10,                \
    }
