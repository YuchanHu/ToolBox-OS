/* Wear levelling and FAT filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   This sample shows how to store files inside a FAT filesystem.
   FAT filesystem is stored in a partition inside SPI flash, using the
   flash wear levelling library.
*/

#include "wear_levelling.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "wear_levelling";

// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

// Mount path for the partition
static const char *base_path = "/spiflash";

void wear_levelling_init(void)
{
    ESP_LOGI(TAG, "Mounting FAT filesystem");
    // Storage moves in v1.2. Recover or provision the volume explicitly over USB.
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files              = 4,
        .format_if_mount_failed = false,
        .allocation_unit_size   = CONFIG_WL_SECTOR_SIZE,
        .use_one_fat            = false,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(base_path, "storage", &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Done");
}

const char *wear_levelling_get_base_path(void)
{
    return base_path;
}
