#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "bluetooth.h"
#include "switch_controller.h"


static const char *TAG = "nvs";


void nvs_read_wifi_credentials(char *read_ssid, char *read_password, char *read_device_id, int8_t *temperature_value,
                                char *read_presence_state, uint16_t *light_value) {
    ESP_LOGI(TAG, "Reading WiFi credentials from NVS");
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS in read-only mode
    err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(err));
        return;
    }
    size_t ssid_len = 0, pass_len = 0, device_id_len = 0, presence_len = 0;
    
    // Read SSID and if provided
    if (read_ssid) {
        err = nvs_get_str(nvs_handle, "ssid", NULL, &ssid_len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SSID not found in NVS");
            read_ssid[0] = '\0';  // Set empty string if not found
        } else {
            err = nvs_get_str(nvs_handle, "ssid", read_ssid, &ssid_len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Read SSID from NVS: %s", read_ssid);
            }
        }
    }
    //Read password if provided
    if (read_password) {
        err = nvs_get_str(nvs_handle, "password", NULL, &pass_len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Password not found in NVS");
            read_password[0] = '\0';  // Set empty string if not found
        } else {
            err = nvs_get_str(nvs_handle, "password", read_password, &pass_len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Read Password from NVS: %s", read_password);
            }
        }
    }

    if(read_device_id) {
        err = nvs_get_str(nvs_handle, "device_id", NULL, &device_id_len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Device ID not found in NVS");
            read_device_id[0] = '\0';  // Set empty string if not found
        } else {
            err = nvs_get_str(nvs_handle, "device_id", read_device_id, &device_id_len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Read Device ID from NVS: %s", read_device_id);
            }
        }
    }

    //Read presence state if provided
    if (read_presence_state) {
        err = nvs_get_str(nvs_handle, "pre_stat", NULL, &presence_len);
        if (err == ESP_OK) {
            err = nvs_get_str(nvs_handle, "pre_stat", read_presence_state, &presence_len);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Read presence state from NVS: %s", read_presence_state);
            }
        } else {
            ESP_LOGW(TAG, "Presence state not found in NVS");
            read_presence_state[0] = '\0';  // Set empty string if not found
        }
    }

    if (temperature_value) {
        err = nvs_get_i8(nvs_handle, "temp_val", temperature_value);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Read temperature value from NVS: %d", *temperature_value);
        } else {
            ESP_LOGW(TAG, "Temperature value not found in NVS");
            *temperature_value = 0;  // Set default value if not found
        }
    }
    if(light_value != NULL) {
        err = nvs_get_u16(nvs_handle, "light_val", light_value);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Read light value from NVS: %d", *light_value);
        } else {
            ESP_LOGW(TAG, "Light value not found in NVS");
            *light_value = 0;  // Set default value if not found
        }
    }

    nvs_close(nvs_handle);
}

void store_wifi_credentials_to_nvs(void) {

    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return;
    }

    // Store SSID
    if(wifi_credentials.ssid != NULL && strlen((char *)wifi_credentials.ssid) > 0) {
        err = nvs_set_str(nvs_handle, "ssid", (const char *)wifi_credentials.ssid);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store SSID to NVS: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return;
        }
    }

    // Store password
    if(wifi_credentials.password){
        err = nvs_set_str(nvs_handle, "password", (const char *)wifi_credentials.password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store password to NVS: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return;
        }
    }
    if(wifi_credentials.device_id){
        err = nvs_set_str(nvs_handle, "device_id", (const char *)wifi_credentials.device_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store device_id to NVS: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return;
        }
    }
    //store presence state
    if(strlen(g_sensor_config.presence_state) > 0) {
        err = nvs_set_str(nvs_handle, "pre_stat", (const char *)g_sensor_config.presence_state);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store presence state to NVS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Presence state stored in NVS successfully: %s", g_sensor_config.presence_state);
        }
    }

    if((g_sensor_config.temperature_value > 15 && g_sensor_config.temperature_value < 45) || g_sensor_config.temperature_value == 0) {
        ESP_LOGI(TAG, "Temperature value: %d", g_sensor_config.temperature_value);
        err = nvs_set_i8(nvs_handle, "temp_val", g_sensor_config.temperature_value);
        if (err != ESP_OK){
            ESP_LOGE(TAG, "Failed to store temperature value to NVS: %s", esp_err_to_name(err));
        }else{
            ESP_LOGI(TAG, "Temperature value stored in NVS successfully: %d", g_sensor_config.temperature_value);
        }
        
    }
    if(g_sensor_config.light_value > 0) {
        err = nvs_set_u16(nvs_handle, "light_val", g_sensor_config.light_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store light value to NVS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Light value stored in NVS successfully: %d", g_sensor_config.light_value);
        }
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "All credentials saved to NVS successfully");
    }

    nvs_close(nvs_handle);
}

void nvs_init(void) {
    ESP_LOGI(TAG, "Initializing NVS flash");
    esp_err_t ret = nvs_flash_init();

    // If NVS partition was truncated or is new, erase it and try again
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
        }
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "NVS initialized successfully");
    }
}