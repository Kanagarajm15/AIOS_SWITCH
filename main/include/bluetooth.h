#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_gatts_api.h"

//macro definitions
#define PROFILE_NUM                 1
#define PROFILE_APP_IDX             0
#define SVC_INST_ID                 0

// Maximum characteristic value length
#define GATTS_DEMO_CHAR_VAL_LEN_MAX 1024
#define PREPARE_BUF_MAX_SIZE        1024
#define CHAR_DECLARATION_SIZE       (sizeof(uint8_t))

#define adv_config_flag      (1 << 0)
#define scan_rsp_config_flag (1 << 1)

// WiFi credentials structure
typedef struct {
    uint8_t *ssid;
    uint8_t *password;
    uint8_t *endpoint;
    uint8_t *api_key;
} wifi_credentials_t;

typedef struct {
    esp_gatt_if_t gatts_if;
    uint16_t conn_id;
} scan_task_params_t;
typedef struct {
    uint8_t                 *prepare_buf;
    int                     prepare_len;
} prepare_type_env_t;

// Global variables - declare as extern
extern volatile bool wifi_creds_ready;
extern volatile bool reconnecting_to_previous;
extern char BLE_DEVICE_NAME[30];
extern char DEVICE_ID[30];  // Add extern declaration for DEVICE_ID
extern EventGroupHandle_t s_wifi_event_group;
extern wifi_credentials_t wifi_credentials;

// Function prototypes
esp_err_t bluetooth_init(void);
void bluetooth_start_advertising(void);
void bluetooth_stop_advertising(void);
void nvs_read_wifi_credentials(char *read_ssid, char *read_password, char *read_endpoint, char *read_api_key);
void store_wifi_credentials_to_nvs(void);
void wifi_init_sta(void);
void scan_wifi_networks(char* response);
void wifi_scan_callback_task(void *pvParameters);
void connect_wifi_with_new_credentials_task(void);      //*pvParameters
void ble_client_send(char *data);
bool check_device_id(const char* received_device_id);
void send_device_verification_response(bool is_verified);

#endif /* BLUETOOTH_H */
