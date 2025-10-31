#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "version.h"
#include "led.h"
#include "bluetooth.h"
#include "nvs.h"
#include "wifi.h"
#include "switch_controller.h"

static const char *TAG = "SWITCH";


void gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN) | (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY_PIN, 0);
    gpio_set_level(LED_PIN, 0);
}



void app_main(void)
{
    // Display version information
    ESP_LOGI(TAG, "Starting application - Firmware Version: %s", SW_FIRMWARE_VERSION);

    // Get MAC address
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    ESP_LOGI(TAG, "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(BLE_DEVICE_NAME, sizeof(BLE_DEVICE_NAME), "SE-16A-SW-%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Set DEVICE_ID using MAC address
    snprintf(DEVICE_ID, sizeof(DEVICE_ID), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Device ID: %s", DEVICE_ID);

    nvs_init();

    rgb_led_init();
    rgb_led_set_red();

    gpio_init();

    // Initialize Bluetooth
    esp_err_t ret = bluetooth_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Bluetooth: %s", esp_err_to_name(ret));
        return;
    }

    wifi_init_sta();

    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_get_ip_info(netif, &ip_info);
        if (ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&ip_info.ip));
        } else {
            ESP_LOGW(TAG, "WiFi connected but no IP address assigned yet");
        }
    } else {
        ESP_LOGW(TAG, "WiFi network interface not available");
    }

    vTaskDelay(pdMS_TO_TICKS(5000));    

    switch_controller_init();

    ESP_LOGI(TAG, "Switch ready to receive commands");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}