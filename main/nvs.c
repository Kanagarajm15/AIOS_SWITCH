#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "bluetooth.h"
#include <string.h>

static const char *TAG = "nvs";

// void nvs_read_wifi_credentials(char *read_ssid, char *read_password, char *read_endpoint, char *read_api_key) {
//     ESP_LOGI(TAG, "Reading WiFi credentials from NVS");
//     strcpy(read_ssid, "kanagaraj");
//     strcpy(read_password, "102030405");
// }

// void store_wifi_credentials_to_nvs(void) {
//     ESP_LOGI(TAG, "Storing WiFi credentials to NVS");

// }

void nvs_read_wifi_credentials(char *read_ssid, char *read_password, char *read_device_id) {
    ESP_LOGI(TAG, "Reading WiFi credentials from NVS");
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // // Validate input parameters
    // if (!read_ssid || !read_password) {
    //     ESP_LOGE(TAG, "Invalid parameters: SSID and password buffers are required");
    //     return;
    // }
    // Open NVS in read-only mode
    err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(err));
        return;
    }
    size_t ssid_len = 0, pass_len = 0, device_id_len = 0;
    // bool success = true;

    // Get lengths and validate existence of required fields
    // err = nvs_get_str(nvs_handle, "ssid", NULL, &ssid_len);
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "SSID not found in NVS");
    //     success = false;
    // }

    // err = nvs_get_str(nvs_handle, "password", NULL, &pass_len);
    // if (err != ESP_OK) {
    //     ESP_LOGW(TAG, "Password not found in NVS");
    //     success = false;
    // }

    // Only proceed if required credentials exist
    // if (success) {
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
        
    // Read endpoint if provided
    // if (read_endpoint) {
    //     err = nvs_get_str(nvs_handle, "endpoint", NULL, &endpoint_len);
    //     if (err == ESP_OK) {
    //         err = nvs_get_str(nvs_handle, "endpoint", read_endpoint, &endpoint_len);
    //         if (err == ESP_OK) {
    //             ESP_LOGI(TAG, "Read endpoint from NVS: %s", read_endpoint);
    //         }
    //     } else {
    //         ESP_LOGW(TAG, "Endpoint not found in NVS");
    //         read_endpoint[0] = '\0';  // Set empty string if not found
    //     }
    // }
    
    // Read API key if provided
    // if (read_api_key) {
    //     err = nvs_get_str(nvs_handle, "api_key", NULL, &api_key_len);
    //     if (err == ESP_OK) {
    //         err = nvs_get_str(nvs_handle, "api_key", read_api_key, &api_key_len);
    //         if (err == ESP_OK) {
    //             ESP_LOGI(TAG, "Read API key from NVS: %s", read_api_key);
    //         }
    //     } else {
    //         ESP_LOGW(TAG, "API key not found in NVS");
    //         read_api_key[0] = '\0';  // Set empty string if not found
    //     }
    // }
    // } else {
    //     // Clear all buffers if required credentials weren't found
    //     read_ssid[0] = '\0';
    //     read_password[0] = '\0';
    //     if (read_endpoint) {
    //         read_endpoint[0] = '\0';
    //     }
    //     if (read_api_key) {
    //         read_api_key[0] = '\0';
    //     }
    // }

    nvs_close(nvs_handle);
}

void store_wifi_credentials_to_nvs(void) {
    if (!wifi_credentials.ssid || !wifi_credentials.password) {
        ESP_LOGE(TAG, "No WiFi credentials to store");
        return;
    }

    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return;
    }

    // Store SSID
    err = nvs_set_str(nvs_handle, "ssid", (const char *)wifi_credentials.ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store SSID to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }

    // Store password
    err = nvs_set_str(nvs_handle, "password", (const char *)wifi_credentials.password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store password to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }

    err = nvs_set_str(nvs_handle, "device_id", (const char *)wifi_credentials.device_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store device_id to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }

    // Store endpoint
    // if (wifi_credentials.endpoint != NULL && strlen((char *)wifi_credentials.endpoint) > 0) {
    //     err = nvs_set_str(nvs_handle, "endpoint", (const char *)wifi_credentials.endpoint);
    //     if (err != ESP_OK) {
    //         ESP_LOGE(TAG, "Failed to store endpoint to NVS: %s", esp_err_to_name(err));
    //     } else {
    //         ESP_LOGI(TAG, "Endpoint stored in NVS successfully: %s", wifi_credentials.endpoint);
    //     }
    // }

    // // Store API key
    // if (wifi_credentials.api_key != NULL && strlen((char *)wifi_credentials.api_key) > 0) {
    //     err = nvs_set_str(nvs_handle, "api_key", (const char *)wifi_credentials.api_key);
    //     if (err != ESP_OK) {
    //         ESP_LOGE(TAG, "Failed to store API key to NVS: %s", esp_err_to_name(err));
    //     } else {
    //         ESP_LOGI(TAG, "API key stored in NVS successfully: %s", wifi_credentials.api_key);
    //     }
    // }
    
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