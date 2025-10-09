#ifndef SWITCH_CONTROLLER_H
#define SWITCH_CONTROLLER_H

#define UDP_PORT 9999
#define RELAY_PIN GPIO_NUM_2

void switch_controller_init();
void process_command(const char* command);
void udp_receiver_task(void *pvParameters);

#endif