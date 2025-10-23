#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "led.h"
#include "nvs.h"
#include "switch_controller.h"

static const char *TAG = "SWITCH_CTRL";

void process_command(const char* command)
{
    if (strcmp(command, "ON") == 0) {
        gpio_set_level(RELAY_PIN, 1);
        gpio_set_level(LED_PIN, 1);
        gpio_set_level(SWITCH_PIN, 1);
        // rgb_led_set_blue();
        ESP_LOGI(TAG, "Switch ON");
    } else if (strcmp(command, "OFF") == 0) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        gpio_set_level(RELAY_PIN, 0);
        gpio_set_level(LED_PIN, 0);
        gpio_set_level(SWITCH_PIN, 0);
        // rgb_led_set_orange();
        ESP_LOGI(TAG, "Switch OFF");
    }
}

void udp_receiver_task(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP receiver listening on port %d", UDP_PORT);

    char buffer[512];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);
    
    //read device id from nvs
    char device_id[32];
    nvs_read_wifi_credentials(NULL,NULL,device_id);

    while (1) {
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                          (struct sockaddr*)&source_addr, &socklen);
        
        if (len > 0) {
            buffer[len] = '\0';
            ESP_LOGI(TAG, "Received: %s", buffer);

            cJSON *json = cJSON_Parse(buffer);
            if (json != NULL) {
                cJSON *command = cJSON_GetObjectItem(json, "command");
                cJSON *source = cJSON_GetObjectItem(json, "source");
                cJSON *sensor_device_id = cJSON_GetObjectItem(json, "device_id");

                ESP_LOGI(TAG, "sensor_device_id: %s", sensor_device_id->valuestring);
                ESP_LOGI(TAG, "device_id: %s", device_id);
                
                if (cJSON_IsString(command) && command->valuestring != NULL) {
                    if (source && cJSON_IsString(source) && 
                        strcmp(source->valuestring, "AIOS_SENSOR") == 0) {
                            if (sensor_device_id && cJSON_IsString(sensor_device_id) && 
                                strcmp(sensor_device_id->valuestring, device_id) == 0) {
                                ESP_LOGI(TAG, "Command from sensor: %s", command->valuestring);
                                process_command(command->valuestring);
                            }
                        
                    }
                }
                cJSON_Delete(json);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void switch_controller_init()
{
   //create task for udp receiver
   xTaskCreate(udp_receiver_task, "udp_receiver_task", 4096, NULL, 5, NULL);
}