#include "wifi.h"
#include "bluetooth.h"
#include "driver/gpio.h"
#include <stdlib.h>
#include "led.h"
#include "nvs.h"

// Define the TAG for logging
static const char *TAG = "wifi_station";


// Define global variables
EventGroupHandle_t s_wifi_event_group;
int s_retry_num = 0;
esp_netif_t *sta_netif = NULL;
bool is_connected = false;


static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Check if we have valid credentials before attempting to connect
        char ssid[100] = {0};
        char password[100] = {0};
     
        // First check if we have credentials in memory (from BLE)
        bool has_credentials_in_memory = (wifi_credentials.ssid != NULL &&
                          wifi_credentials.password != NULL &&
                          strlen((char*)wifi_credentials.ssid) > 0);
        
        if (has_credentials_in_memory) {
            // We have credentials in memory, use them directly
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START: Using credentials from memory");
            esp_wifi_connect();
        } else {
            // Try to read from NVS as fallback
            nvs_read_wifi_credentials(ssid, password, NULL, NULL, NULL);

            if (strlen(ssid) > 0) {
                esp_wifi_connect();
                ESP_LOGI(TAG, "WIFI_EVENT_STA_START: Attempting to connect to WiFi using NVS credentials");
            } else {
                ESP_LOGI(TAG, "WIFI_EVENT_STA_START: No credentials available, not connecting");
                // Set the FAIL bit to indicate we're not connecting
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;

        // Log the reason for disconnection
        ESP_LOGI(TAG, "WiFi disconnected, reason: %d", event->reason);

        // Handle different disconnection reasons
        switch (event->reason) {            case WIFI_REASON_AUTH_FAIL:
                ESP_LOGE(TAG, "WiFi authentication failed - check password");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                s_retry_num = 5; // Force stop retrying for auth failures
                // rgb_led_set_red(); // Red LED for disconnected
                break;
            case WIFI_REASON_NO_AP_FOUND:
                ESP_LOGE(TAG, "WiFi AP not found - check SSID or AP availability");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                s_retry_num = 5; // Force stop retrying for AP not found
                // rgb_led_set_red(); // Red LED for disconnected
                break;
            case WIFI_REASON_BEACON_TIMEOUT:
                ESP_LOGE(TAG, "WiFi beacon timeout - AP may be too far or congested");
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                s_retry_num = 5; // Force stop retrying for beacon timeout
                // rgb_led_set_red(); // Red LED for disconnected
                break;
            default:
                ESP_LOGE(TAG, "WiFi disconnected with reason: %d", event->reason);
                // rgb_led_set_red(); // Red LED for disconnected
                break;
        }
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        // rgb_led_set_green(); // Green LED for connected
    }
}

void scan_wifi_networks(char* response) {
    ESP_LOGI(TAG, "Starting WiFi scan...");
    // Force stop any ongoing connection attempts
    esp_wifi_disconnect();
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // Ensure WiFi is in station mode
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        ESP_LOGE(TAG, "WiFi not in station mode");
        snprintf(response, 1000, "{\"event_type\":\"scan_list\",\"data\":{\"error\":\"WiFi not in station mode\"}}");
        return;
    }

    // Reset WiFi state
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

    // Stop any ongoing scan
    esp_wifi_scan_stop();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    // Try to start scan with retries
    esp_err_t err;
    int retry_count = 0;
    do {
        err = esp_wifi_scan_start(&scan_config, true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Scan attempt %d failed: %s", retry_count + 1, esp_err_to_name(err));
            vTaskDelay(200 / portTICK_PERIOD_MS);

            // Additional reset if scan fails
            esp_wifi_stop();
            vTaskDelay(100 / portTICK_PERIOD_MS);
            esp_wifi_start();
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        retry_count++;
    } while (err != ESP_OK && retry_count < 3);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed after all retries: %s", esp_err_to_name(err));
        snprintf(response, 1000, "{\"event_type\":\"scan_list\",\"data\":{\"error\":\"%s\"}}", esp_err_to_name(err));
        return;
    }

    uint16_t ap_num = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));

    if (ap_num > 15) {
        ap_num = 15;
        ESP_LOGW(TAG, "Limiting APs to 15");
    }
    if (ap_num > 0) {
        wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_num);
        if (!ap_records) {
            ESP_LOGE(TAG, "Memory allocation failed for AP records");
            snprintf(response, 1000, "{\"event_type\":\"scan_list\",\"data\":{\"error\":\"malloc_failed_ap_records\"}}");
            return;
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));

        // Start building the JSON response with the exact format requested
        strcpy(response, "{\"event_type\":\"scan_list\",\"data\":{");
        int offset = strlen(response);

        bool first = true;
        for (int i = 0; i < ap_num; i++) {
            if (strlen((char *)ap_records[i].ssid) == 0) continue;

            if (!first) {
                offset += snprintf(response + offset, 1000 - offset, ",");
            }

            // Format exactly as requested: "SSID":"-RSSI"
            offset += snprintf(response + offset, 1000 - offset,
                  "\"%s\":\"%d\"",
                  ap_records[i].ssid,
                  ap_records[i].rssi);
            first = false;

            if (offset > 900) {
                ESP_LOGW(TAG, "JSON buffer almost full, truncating...");
                break;
            }
        }

        // Close the JSON object
        snprintf(response + offset, 1000 - offset, "}}");
        free(ap_records);
    } else {
        // No APs found, return empty data object
        snprintf(response, 1000, "{\"event_type\":\"scan_list\",\"data\":{}}");
    }

    // Print response for debugging
    ESP_LOGI(TAG, "Scan Response: %s", response);
    ESP_LOGI(TAG, "Response length: %d", strlen(response));

    // If we were connected before, try to reconnect
    if (is_connected) {
        esp_wifi_connect();
    }
}
void wifi_init_sta(void)
{
    static bool is_initialized = false;

    s_wifi_event_group = xEventGroupCreate();    // Initialize RGB LED for WiFi status indication
    // rgb_led_set_red();  // Initially on (disconnected state)

    if (!is_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        // Fix: Use the correct field name
        // cfg.beacon_max_time = 1000; // Increase beacon timeout (default is 600)
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        is_initialized = true;

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &event_handler,
                                                            NULL,
                                                            &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    }
    char ssid[100] = {0};
    char password[100] = {0};
    ESP_LOGI(TAG, "WiFi Credentials");
    nvs_read_wifi_credentials(ssid,password, NULL, NULL, NULL);
    ESP_LOGI(TAG, "*******SSID for wifi: %s", ssid);
    ESP_LOGI(TAG, "*******Password for wifi: %s", password);

    wifi_config_t wifi_config = {0};

    // Copy the SSID and password to the wifi_config structure
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    // Log the length of the strings after copying
    ESP_LOGI(TAG, "Length of wifi_config.sta.ssid: %d characters", strlen((char*)wifi_config.sta.ssid));
    ESP_LOGI(TAG, "Length of wifi_config.sta.password: %d characters", strlen((char*)wifi_config.sta.password));

    // Set authentication mode
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // Improve connection stability
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // Set channel to 0 to scan all channels
    wifi_config.sta.channel = 0;

    // Set scan method to active for better AP discovery
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;

    // Increase connection timeout
    wifi_config.sta.listen_interval = 3;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Only set WiFi config and attempt to connect if we have valid credentials
    if (strlen(ssid) > 0) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    } else {
        ESP_LOGI(TAG, "No WiFi credentials available, not attempting to connect");
        // Set the FAIL bit to indicate we're not connecting
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }

    // Start WiFi first
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi_init_sta finished.");

    // Set WiFi power save mode to NONE for better stability
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Configure additional WiFi parameters to help with connection issues
    // Increase beacon timeout to help with connection issues
    ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_STA, 10));

    // Set protocol to support all modes
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));

    // Set country info for proper channel configuration
    wifi_country_t country = {
        .cc = "00",  // Worldwide country code
        .schan = 1,
        .nchan = 11, // Channels 1-13
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ssid, password);
        is_connected = true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ssid, password);
        is_connected = false;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        is_connected = false;
    }
}

void wifi_scan_task(void *pvParameters) {
    char response[1000] = {0};

    while (1) {
        // Only scan if we're connected to BLE client
        if (interface_type != 0) {
            ESP_LOGI(TAG, "Scanning WiFi networks for connected BLE client");
            memset(response, 0, sizeof(response));
            scan_wifi_networks(response);
            // Send to BLE client
            if (strlen(response) > 0) {
                // ble_client_send(response);
                ESP_LOGI(TAG, "Sent WiFi scan results to BLE client");
            }
        } else {
            ESP_LOGD(TAG, "No BLE client connected, skipping WiFi scan");
        }
        // Wait before next scan (5 seconds)
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}