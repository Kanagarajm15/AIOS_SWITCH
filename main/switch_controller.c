#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "led.h"
#include "nvs.h"
#include "main.h"
#include "switch_controller.h"

static const char *TAG = "SWITCH_CTRL";

/* ---------------- Configuration ---------------- */
#define TEMP_DELAY_MS     60000   // 1 minute for TEMP origin
#define MOTION_DELAY_MS   5000    // 5 seconds for MOTION origin
#define DEFAULT_DELAY_MS  6000    // Default delay
#define UDP_BUFFER_SIZE   512

/* ---------------- Global Variables ---------------- */
static TimerHandle_t off_timer = NULL;

static uint32_t current_delay_ms = DEFAULT_DELAY_MS;
static bool current_switch_state = false; // false = OFF, true = ON
static bool last_command_was_on = false; // Track last command to avoid duplicates

int8_t g_temperature_threshold = 0; // Global temperature threshold
char g_switch_mode[10] = "OFF"; // Default to Auto mode
char g_device_id[32] = {0}; // Global device ID

/* Button handling */
static QueueHandle_t gpio_evt_queue = NULL;
static const TickType_t DEBOUNCE_TICKS = pdMS_TO_TICKS(50);

sensor_config_t g_sensor_config;

/* ---------------- Updating Functions ---------------- */
void update_temperature_threshold(int8_t new_threshold) {
    g_temperature_threshold = new_threshold;
    ESP_LOGI(TAG, "Temperature threshold updated to: %d", g_temperature_threshold);
}

void update_presence_switch_state(char *new_state){
    strncpy(g_switch_mode, new_state, sizeof(g_switch_mode) - 1);
    g_switch_mode[sizeof(g_switch_mode) - 1] = '\0';
    ESP_LOGI(TAG, "Presence switch state updated to: %s", g_switch_mode);
}
/* ---------------- Helper Functions ---------------- */
void set_switch_state(bool on)
{
    int level = on ? 1 : 0;
    ESP_LOGI(TAG, "Setting physical RELAY/LED to %d", level);
    // Set relay and status LED GPIOs
    gpio_set_level(RELAY_PIN, level);
    gpio_set_level(LED_PIN, level);

    current_switch_state = on;
    // if(on){
    //     rgb_led_set_blue();
    // }else{
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

/* ---------------- Sensor Data Processor ---------------- */
void process_sensor_data(const char *sensor_json)
{
    if (!sensor_json) {
        ESP_LOGW(TAG, "Invalid sensor data");
        return;
    }

    cJSON *sensor_data = cJSON_Parse(sensor_json);
    if (!sensor_data) {
        ESP_LOGW(TAG, "Failed to parse sensor JSON");
        return;
    }

    cJSON *presence = cJSON_GetObjectItem(sensor_data, "presence_detected");
    cJSON *temperature = cJSON_GetObjectItem(sensor_data, "temperature");

    bool presence_detected = presence && cJSON_IsBool(presence) ? cJSON_IsTrue(presence) : false;
    float temp_value = temperature && cJSON_IsNumber(temperature) ? (float)cJSON_GetNumberValue(temperature) : 0.0;

    ESP_LOGI(TAG, "Sensor data - Presence: %s, Temp: %.2f°C, Threshold: %d°C", 
             presence_detected ? "YES" : "NO", temp_value, g_temperature_threshold);

    // Determine switch action based on conditions
    bool should_turn_on = false;
    const char* trigger_reason = "NONE";
    
    if (g_temperature_threshold != 0) {
        if (temp_value >= g_temperature_threshold) {
            // Temperature exceeds threshold
            ESP_LOGW(TAG, "Temperature %.2f°C exceeds threshold %d°C", temp_value, g_temperature_threshold);
            if (strcmp(g_switch_mode, "ON") == 0) {
                // Switch mode ON + temp exceeded -> use presence for decision
                if (presence_detected) {
                    should_turn_on = true;
                    trigger_reason = "PRESENCE";
                    current_delay_ms = MOTION_DELAY_MS;
                } else {
                    should_turn_on = false;
                    trigger_reason = "PRESENCE"; // temp ON but above threshold + presence OFF -> OFF (reason: PRESENCE)
                    current_delay_ms = MOTION_DELAY_MS;
                }
            } else {
                // Switch mode OFF + temp exceeded -> turn OFF
                should_turn_on = false;
                trigger_reason = "PRESENCE";
                current_delay_ms = MOTION_DELAY_MS;
            }
        } else {
            // Temperature below threshold -> turn OFF
            should_turn_on = false;
            trigger_reason = "TEMP"; // temp ON but below threshold -> OFF (reason: TEMP)
            current_delay_ms = TEMP_DELAY_MS;
        }
    } else {
        // No temperature threshold set - use switch mode
        if (strcmp(g_switch_mode, "ON") == 0) {
            if (presence_detected) {
                should_turn_on = true;
                trigger_reason = "PRESENCE";
                current_delay_ms = MOTION_DELAY_MS;
            } else {
                should_turn_on = false;
                trigger_reason = "PRESENCE";
                current_delay_ms = MOTION_DELAY_MS;
            }
        } else {
            // Switch mode OFF -> turn OFF
            should_turn_on = false;
            trigger_reason = "PRESENCE";
            current_delay_ms = MOTION_DELAY_MS;
        }
    }

    // Execute command with deduplication
    if (should_turn_on) {
        // ON command - execute only if not already ON
        if (!last_command_was_on) {
            if (off_timer && xTimerIsTimerActive(off_timer)) {
                xTimerStop(off_timer, 0);
                ESP_LOGI(TAG, "OFF timer stopped");
            }
            set_switch_state(true);
            last_command_was_on = true;
            ESP_LOGI(TAG, "Switch ON triggered by %s", trigger_reason);
        } else {
            ESP_LOGD(TAG, "ON command ignored - already ON");
        }
    } else {
        // OFF command - execute only if not already processing OFF
        if (last_command_was_on) {
            if (off_timer) {
                ESP_LOGI(TAG, "Starting OFF timer - %s (delay: %lu ms)", trigger_reason, current_delay_ms);
                xTimerStop(off_timer, 0);
                xTimerChangePeriod(off_timer, pdMS_TO_TICKS(current_delay_ms), 0);
                xTimerStart(off_timer, 0);
                last_command_was_on = false;
            }
        } else {
            ESP_LOGD(TAG, "OFF command ignored - already processing OFF");
        }
    }
    
    cJSON_Delete(sensor_data);
}

/* ---------------- Button ISR and task ---------------- */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (gpio_evt_queue) {
        xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void button_task(void *pvParameter)
{
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            // Debounce: wait briefly then confirm state
            vTaskDelay(DEBOUNCE_TICKS);
            int level = gpio_get_level((gpio_num_t)io_num);
            // Button is active LOW: pressed -> level == 0
            if (level == 0) {
                // Toggle relay and LED
                bool new_state = !current_switch_state;
                set_switch_state(new_state);
                ESP_LOGI(TAG, "Button press detected on GPIO %lu, toggled to %s", io_num, new_state ? "ON" : "OFF");
            } else {
                // Release or bounce - ignore
                ESP_LOGW(TAG, "GPIO %lu event but level=%d (ignored)", io_num, level);
            }
        }
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

    ESP_LOGI(TAG, "Using Device ID: %s", g_device_id);

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
                // cJSON *origin  = cJSON_GetObjectItem(json, "origin");
                cJSON *sensor_device_id = cJSON_GetObjectItem(json, "device_id");

                if (command && cJSON_IsString(command) &&
                    source && cJSON_IsString(source) &&
                    sensor_device_id && cJSON_IsString(sensor_device_id)) {

                    ESP_LOGI(TAG, "Parsed JSON - command: %s, sensor_id: %s",
                             command->valuestring, sensor_device_id->valuestring);

                    if (strcmp(source->valuestring, "AIOS_SENSOR") == 0 &&
                        strcmp(sensor_device_id->valuestring, g_device_id) == 0) {

                        ESP_LOGI(TAG, "Valid sensor data from matching device");
                        process_sensor_data(command->valuestring);
                    } else {
                        //print log for source and deviceid
                        ESP_LOGE(TAG, "source: %s, device_id: %s", source->valuestring, g_device_id);
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
    // Load settings from NVS directly into global variables
    nvs_read_wifi_credentials(NULL, NULL, g_device_id, &g_temperature_threshold, g_switch_mode);
    
    ESP_LOGI(TAG, "Loaded settings from NVS - Device ID: %s, Temp Threshold: %d, Switch Mode: %s", 
             g_device_id, g_temperature_threshold, g_switch_mode);
    
    // Configure switch pin as input with pull-up and falling-edge interrupt
    gpio_reset_pin(SWITCH_PIN);
    gpio_set_direction(SWITCH_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SWITCH_PIN, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(SWITCH_PIN, GPIO_INTR_NEGEDGE);

    // Create queue for gpio events
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (gpio_evt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create gpio event queue");
    }

    // Install ISR service and add handler (install once globally is fine)
    gpio_install_isr_service(0);
    gpio_isr_handler_add(SWITCH_PIN, gpio_isr_handler, (void *)SWITCH_PIN);

    // Create button task (lower priority so network/UDP processing isn't starved)
    if (xTaskCreate(button_task, "button_task", 2048, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
    } else {
        ESP_LOGI(TAG, "Button task started");
    }

    // Create delayed OFF timer
    off_timer = xTimerCreate("off_timer", pdMS_TO_TICKS(DEFAULT_DELAY_MS),
                             pdFALSE, NULL, off_timer_callback);

    if (off_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create OFF timer!");
    } else {
        ESP_LOGI(TAG, "OFF timer created successfully");
    }

    // Start UDP receiver task (give slightly higher priority than button task)
    if (xTaskCreate(udp_receiver_task, "udp_receiver_task", 4096, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UDP receiver task");
    } else {
        ESP_LOGI(TAG, "UDP receiver task started");
    }

    // Initialize current_switch_state from physical relay pin
    current_switch_state = (gpio_get_level(RELAY_PIN) != 0);
}
