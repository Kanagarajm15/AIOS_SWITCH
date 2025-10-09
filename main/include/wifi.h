#ifndef WIFI_H
#define WIFI_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"

// WiFi connection status bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAXIMUM_RETRY       5

// WiFi scan parameters
#define MAX_AP_COUNT        15
#define SCAN_LIST_SIZE      1024

// BLE characteristic index
#define IDX_CHAR_VAL_B      3

// WiFi credentials structure
// typedef struct {
//     char ssid[32];
//     char password[64];
// } wifi_credentials_t;

// External variable declarations
extern EventGroupHandle_t s_wifi_event_group;
extern int s_retry_num;
extern esp_netif_t *sta_netif;
extern bool is_connected;
extern esp_gatt_if_t interface_type;
extern uint16_t heart_rate_handle_table[];
// extern wifi_credentials_t wifi_credentials;

// Function declarations
void wifi_init_sta(void);
void scan_wifi_networks(char* response);
void connect_wifi_with_new_credentials_task(void);

#endif /* WIFI_H */
