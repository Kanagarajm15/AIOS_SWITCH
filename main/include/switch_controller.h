#ifndef SWITCH_CONTROLLER_H
#define SWITCH_CONTROLLER_H

#define UDP_PORT 9999
#define RELAY_PIN GPIO_NUM_3
#define LED_PIN GPIO_NUM_7
#define SWITCH_PIN GPIO_NUM_5

void switch_controller_init();
void process_command(const char* command, const char* origin);
void udp_receiver_task(void *pvParameters);
void set_switch_state(bool on);

#endif