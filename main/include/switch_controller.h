#ifndef SWITCH_CONTROLLER_H
#define SWITCH_CONTROLLER_H

#define UDP_PORT 9999
#define RELAY_PIN GPIO_NUM_3
#define LED_PIN GPIO_NUM_7
#define SWITCH_PIN GPIO_NUM_5

typedef struct {
    char presence_state[10];     // "ON"/"OFF"
    int8_t temperature_value;
    uint16_t light_value;        // Changed to uint16_t for 0-3000 range
} sensor_config_t;

extern sensor_config_t g_sensor_config;

void switch_controller_init();
void process_command(const char* command, const char* origin);
void udp_receiver_task(void *pvParameters);
void set_switch_state(bool on);
void update_temperature_threshold(int8_t new_threshold);
void update_presence_switch_state(char *new_state);
void update_light_threshold(uint16_t new_threshold);

#endif