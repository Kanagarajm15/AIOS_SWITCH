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

/* ---------------- Configuration ---------------- */
#define TEMP_DELAY_MS     60000   // 1 minute for TEMP origin
#define MOTION_DELAY_MS   5000    // 5 seconds for MOTION origin
#define DEFAULT_DELAY_MS  6000    // Default delay
#define UDP_BUFFER_SIZE   512

/* ---------------- Global Variables ---------------- */
static TimerHandle_t off_timer = NULL;
static bool last_command_was_off = false;
static uint32_t current_delay_ms = DEFAULT_DELAY_MS;

/* ---------------- Helper Functions ---------------- */
void set_switch_state(bool on)
{
    int level = on ? 1 : 0;
    // printf("Setting switch state to %d and %d\n", level, on);
    gpio_set_level(RELAY_PIN, level);
    gpio_set_level(LED_PIN, level);
    gpio_set_level(SWITCH_PIN, level);
    // if(on) {
    //     rgb_led_set_blue();
    // } else {
    //     rgb_led_set_orange();
    // }

    ESP_LOGI(TAG, "Switch %s", on ? "ON" : "OFF");
}

/* ---------------- Timer Callback ---------------- */
static void off_timer_callback(TimerHandle_t xTimer)
{
    set_switch_state(false);
    ESP_LOGI(TAG, "Switch OFF (delayed)");
}

/* ---------------- Command Processor ---------------- */
void process_command(const char *command, const char *origin)
{
    if (!command || !origin) {
        ESP_LOGW(TAG, "Invalid command or origin");
        return;
    }

    if (strcmp(command, "ON") == 0) {
        // Stop any pending OFF timer
        if (off_timer && xTimerIsTimerActive(off_timer)) {
            xTimerStop(off_timer, 0);
            ESP_LOGI(TAG, "OFF timer stopped");
        }

        set_switch_state(true);
        last_command_was_off = false;
    } 
    else if (strcmp(command, "OFF") == 0) {
        // Determine delay based on origin
        if (strcmp(origin, "TEMP") == 0) {
            current_delay_ms = TEMP_DELAY_MS;
        } else if (strcmp(origin, "MOTION") == 0) {
            current_delay_ms = MOTION_DELAY_MS;
        } else {
            current_delay_ms = DEFAULT_DELAY_MS;
        }

        if (off_timer) {
            if (!last_command_was_off) {
                // First OFF command
                ESP_LOGI(TAG, "First OFF from %s - delay %lu ms", origin, current_delay_ms);
                xTimerChangePeriod(off_timer, pdMS_TO_TICKS(current_delay_ms), 0);
                xTimerStart(off_timer, 0);
                last_command_was_off = true;
            } else {
                // Subsequent OFF command - update delay
                ESP_LOGI(TAG, "Subsequent OFF from %s - updating delay to %lu ms", origin, current_delay_ms);
                xTimerChangePeriod(off_timer, pdMS_TO_TICKS(current_delay_ms), 0);
            }
        } else {
            ESP_LOGE(TAG, "OFF timer not initialized!");
        }
    } 
    else {
        ESP_LOGW(TAG, "Unknown command: %s", command);
    }
}

/* ---------------- UDP Receiver Task ---------------- */
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

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP receiver listening on port %d", UDP_PORT);

    char buffer[UDP_BUFFER_SIZE];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    // Read device ID from NVS
    char device_id[32] = {0};
    nvs_read_wifi_credentials(NULL, NULL, device_id);
    ESP_LOGI(TAG, "Local Device ID: %s", device_id);

    while (1) {
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                           (struct sockaddr *)&source_addr, &socklen);

        if (len > 0) {
            buffer[len] = '\0';
            ESP_LOGI(TAG, "Received UDP: %s", buffer);

            cJSON *json = cJSON_Parse(buffer);
            if (json) {
                cJSON *command = cJSON_GetObjectItem(json, "command");
                cJSON *source  = cJSON_GetObjectItem(json, "source");
                cJSON *origin  = cJSON_GetObjectItem(json, "origin");
                cJSON *sensor_device_id = cJSON_GetObjectItem(json, "device_id");

                if (command && cJSON_IsString(command) &&
                    source && cJSON_IsString(source) &&
                    origin && cJSON_IsString(origin) &&
                    sensor_device_id && cJSON_IsString(sensor_device_id)) {

                    ESP_LOGI(TAG, "Parsed JSON - command: %s, origin: %s, sensor_id: %s",
                             command->valuestring, origin->valuestring, sensor_device_id->valuestring);

                    if (strcmp(source->valuestring, "AIOS_SENSOR") == 0 &&
                        strcmp(sensor_device_id->valuestring, device_id) == 0) {

                        ESP_LOGI(TAG, "Valid command from matching sensor");
                        process_command(command->valuestring, origin->valuestring);
                    } else {
                        ESP_LOGW(TAG, "Ignored packet (source/device mismatch)");
                    }

                } else {
                    ESP_LOGW(TAG, "Invalid or missing JSON fields");
                }

                cJSON_Delete(json);
            } else {
                ESP_LOGW(TAG, "Failed to parse JSON");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ---------------- Initialization ---------------- */
void switch_controller_init(void)
{
    // Create delayed OFF timer
    off_timer = xTimerCreate("off_timer", pdMS_TO_TICKS(DEFAULT_DELAY_MS),
                             pdFALSE, NULL, off_timer_callback);

    if (off_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create OFF timer!");
    } else {
        ESP_LOGI(TAG, "OFF timer created successfully");
    }

    // Start UDP receiver task
    if (xTaskCreate(udp_receiver_task, "udp_receiver_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UDP receiver task");
    } else {
        ESP_LOGI(TAG, "UDP receiver task started");
    }
}
