#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "led.h"
#include "nvs.h"
#include "switch_controller.h"

static const char *TAG = "SWITCH_CTRL";
static TimerHandle_t off_timer = NULL;
static bool last_command_was_off = false;
#define OFF_DELAY_MS 10000  // 10 seconds delay for OFF command

void off_timer_callback(TimerHandle_t xTimer)
{
    gpio_set_level(RELAY_PIN, 0);
    gpio_set_level(LED_PIN, 0);
    gpio_set_level(SWITCH_PIN, 0);
    // rgb_led_set_orange();
    ESP_LOGI(TAG, "Switch OFF (delayed)");
}

void process_command(const char* command)
{
    if (strcmp(command, "ON") == 0) {
        if (off_timer && xTimerIsTimerActive(off_timer)) {
            xTimerStop(off_timer, 0);
            ESP_LOGI(TAG, "OFF timer stopped");
        }
        gpio_set_level(RELAY_PIN, 1);
        gpio_set_level(LED_PIN, 1);
        gpio_set_level(SWITCH_PIN, 1);
        // rgb_led_set_blue();
        ESP_LOGI(TAG, "Switch ON");
        last_command_was_off = false;
    } else if (strcmp(command, "OFF") == 0) {
        if (!last_command_was_off) {
            // First OFF command - start timer
            ESP_LOGI(TAG, "First OFF command - starting %dms delay", OFF_DELAY_MS);
            if (off_timer) {
                xTimerStart(off_timer, 0);
            }
            last_command_was_off = true;
        } else {
            // Subsequent OFF commands - do nothing, let timer continue
            ESP_LOGI(TAG, "Subsequent OFF command - timer continues");
        }
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
   // Create timer for delayed OFF
   off_timer = xTimerCreate("off_timer", pdMS_TO_TICKS(OFF_DELAY_MS), pdFALSE, NULL, off_timer_callback);
   if (off_timer == NULL) {
       ESP_LOGE(TAG, "Failed to create timer!");
   } else {
       ESP_LOGI(TAG, "Timer created successfully");
   }
   //create task for udp receiver
   xTaskCreate(udp_receiver_task, "udp_receiver_task", 4096, NULL, 5, NULL);
}