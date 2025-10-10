#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_sntp.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "bluetooth.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "bluetooth";

// Authentication state
static bool is_authenticated = false;
static const char* AUTH_KEY = "BLAZE";

#define GATTS_TABLE_TAG "GATTS_TABLE_DEMO"

// Define global variables declared as extern in the header
volatile bool wifi_creds_ready = false;
volatile bool reconnecting_to_previous = false;
char BLE_DEVICE_NAME[30] = {0};
char DEVICE_ID[30] = {0};

static uint8_t service_uuid[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

/* The length of adv data must be less than 31 bytes */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = false,
    .include_txpower     = false,
    .min_interval        = 0x0006, 
    .max_interval        = 0x0010, 
    .appearance          = 0x00,
    .manufacturer_len    = 0,    
    .p_manufacturer_data = NULL, 
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(service_uuid),
    .p_service_uuid      = service_uuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// scan response data
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = true,
    .include_txpower     = false,  // Disable to save space
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0x00,
    .manufacturer_len    = 0, 
    .p_manufacturer_data = NULL, 
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,      // Remove service UUID from scan response to save space
    .p_service_uuid      = NULL,
    .flag = 0,  // No flags needed in scan response
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min         = 0x20,
    .adv_int_max         = 0x40,
    .adv_type            = ADV_TYPE_IND,
    .own_addr_type       = BLE_ADDR_TYPE_PUBLIC,
    .channel_map         = ADV_CHNL_ALL,
    .adv_filter_policy   = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Service index
enum {
    IDX_SVC,
    IDX_CHAR_A,
    IDX_CHAR_VAL_A,
    IDX_CHAR_CFG_A,
    IDX_CHAR_B,
    IDX_CHAR_VAL_B,
    IDX_CHAR_CFG_B,
    HRS_IDX_NB,
};

static uint8_t adv_config_done = 0;

uint16_t heart_rate_handle_table[HRS_IDX_NB];

esp_gatt_if_t interface_type = 0;
uint16_t conn_id = 0;
char pair_status = 0;
char data_validation[300] = {0,};

wifi_credentials_t wifi_credentials;
static prepare_type_env_t prepare_write_env_b;
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
    esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst heart_rate_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

// Service UUID
static const uint8_t GATTS_SERVICE_UUID_TEST[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0xbe, 0x10, 0x14, 0x43, 0x22, 0x39, 0x3e, 0xb1, 0xe5, 0xd3, 0xa5, 0xd5, 0x00, 0x08, 0x3e, 0xd4
};

// Characteristic UUIDs
static const uint8_t GATTS_CHAR_UUID_TEST_A[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0xbe, 0x10, 0x14, 0x43, 0x22, 0x39, 0x3e, 0xb1, 0xe5, 0xd3, 0xa5, 0xd5, 0x11, 0x08, 0x3e, 0xd4
};

static const uint8_t GATTS_CHAR_UUID_TEST_B[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0xbe, 0x10, 0x14, 0x43, 0x22, 0x39, 0x3e, 0xb1, 0xe5, 0xd3, 0xa5, 0xd5, 0x22, 0x08, 0x3e, 0xd4
};

static const uint16_t primary_service_uuid         = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid   = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t char_prop_read_notify   = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t data_ccc[2]      = {0x00, 0x00};
static const uint8_t char_value[4]    = {0x11, 0x22, 0x33, 0x44};

/* Full Database Description - Used to add attributes into the database */
static const esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] =
{
    // Service Declaration - Allow reading without encryption
    [IDX_SVC]        =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
      sizeof(uint16_t), sizeof(GATTS_SERVICE_UUID_TEST), (uint8_t *)&GATTS_SERVICE_UUID_TEST}},

    /* Characteristic Declaration - Allow reading without encryption */
    [IDX_CHAR_A]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    /* Characteristic Value - Allow reading and writing without encryption */
    [IDX_CHAR_VAL_A] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_TEST_A, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor - Allow reading and writing without encryption */
    [IDX_CHAR_CFG_A]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(uint16_t), sizeof(data_ccc), (uint8_t *)data_ccc}},

    /* Characteristic Declaration - Allow reading without encryption */
    [IDX_CHAR_B]      =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    /* Characteristic Value - Allow reading and writing without encryption */
    [IDX_CHAR_VAL_B]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_TEST_B, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

	  /* Client Characteristic Configuration Descriptor - Allow reading and writing without encryption */
	[IDX_CHAR_CFG_B]  =
	{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
	  sizeof(uint16_t), sizeof(data_ccc), (uint8_t *)data_ccc}},

};

static char *esp_auth_req_to_str(esp_ble_auth_req_t auth_req)
{
   char *auth_str = NULL;
   switch(auth_req) {
    case ESP_LE_AUTH_NO_BOND:
        auth_str = "ESP_LE_AUTH_NO_BOND";
        break;
    case ESP_LE_AUTH_BOND:
        auth_str = "ESP_LE_AUTH_BOND";
        break;
    case ESP_LE_AUTH_REQ_MITM:
        auth_str = "ESP_LE_AUTH_REQ_MITM";
        break;
    case ESP_LE_AUTH_REQ_BOND_MITM:
        auth_str = "ESP_LE_AUTH_REQ_BOND_MITM";
        break;
    case ESP_LE_AUTH_REQ_SC_ONLY:
        auth_str = "ESP_LE_AUTH_REQ_SC_ONLY";
        break;
    case ESP_LE_AUTH_REQ_SC_BOND:
        auth_str = "ESP_LE_AUTH_REQ_SC_BOND";
        break;
    case ESP_LE_AUTH_REQ_SC_MITM:
        auth_str = "ESP_LE_AUTH_REQ_SC_MITM";
        break;
    case ESP_LE_AUTH_REQ_SC_MITM_BOND:
        auth_str = "ESP_LE_AUTH_REQ_SC_MITM_BOND";
        break;
    default:
        auth_str = "INVALID BLE AUTH REQ";
        break;
   }
   return auth_str;
}

static char *esp_key_type_to_str(esp_ble_key_type_t key_type)
{
   char *key_str = NULL;
   switch(key_type) {
    case ESP_LE_KEY_NONE:
        key_str = "ESP_LE_KEY_NONE";
        break;
    case ESP_LE_KEY_PENC:
        key_str = "ESP_LE_KEY_PENC";
        break;
    case ESP_LE_KEY_PID:
        key_str = "ESP_LE_KEY_PID";
        break;
    case ESP_LE_KEY_PCSRK:
        key_str = "ESP_LE_KEY_PCSRK";
        break;
    case ESP_LE_KEY_PLK:
        key_str = "ESP_LE_KEY_PLK";
        break;
    case ESP_LE_KEY_LLK:
        key_str = "ESP_LE_KEY_LLK";
        break;
    case ESP_LE_KEY_LENC:
        key_str = "ESP_LE_KEY_LENC";
        break;
    case ESP_LE_KEY_LID:
        key_str = "ESP_LE_KEY_LID";
        break;
    case ESP_LE_KEY_LCSRK:
        key_str = "ESP_LE_KEY_LCSRK";
        break;
    default:
        key_str = "INVALID BLE KEY TYPE";
        break;
   }
   return key_str;
}

static void __attribute__((unused)) remove_all_bonded_devices(void)
{
    int dev_num = esp_ble_get_bond_device_num();
#if DEBUG_PRINT_EN
    printf("\nBonded Device number = %d\n", dev_num);
#endif
    esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
    esp_ble_get_bond_device_list(&dev_num, dev_list);
    for (int i = 0; i < dev_num; i++) {
        esp_ble_remove_bond_device(dev_list[i].bd_addr);
    }

    free(dev_list);
}

static void show_bonded_devices(void)
{
    int dev_num = esp_ble_get_bond_device_num();

    esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
    esp_ble_get_bond_device_list(&dev_num, dev_list);
    ESP_LOGI(GATTS_TABLE_TAG, "Bonded devices number : %d\n", dev_num);

    ESP_LOGI(GATTS_TABLE_TAG, "Bonded devices list : %d\n", dev_num);
    for (int i = 0; i < dev_num; i++) {
        esp_log_buffer_hex(GATTS_TABLE_TAG, (void *)dev_list[i].bd_addr, sizeof(esp_bd_addr_t));
    }

    free(dev_list);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    #ifdef CONFIG_SET_RAW_ADV_DATA
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~adv_config_flag);
            if (adv_config_done == 0){
				printf("\n ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT adv start");
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~scan_rsp_config_flag);
            if (adv_config_done == 0){
				printf("\n case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:adv start");
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
    #else
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            adv_config_done &= (~adv_config_flag);
            if (adv_config_done == 0){
                ESP_LOGI(GATTS_TABLE_TAG, "ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT - starting advertising");
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
			printf("\n ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT--------");
            adv_config_done &= (~scan_rsp_config_flag);
			if (adv_config_done == 0) {
				esp_ble_gap_start_advertising(&adv_params);
			}
            break;
    #endif

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            /* advertising start complete event to indicate advertising start successfully or failed */
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "advertising start failed, status: %d", param->adv_start_cmpl.status);
            }else{
                ESP_LOGI(GATTS_TABLE_TAG, "advertising start successfully");


            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "Advertising stop failed");
            }
            else {
                ESP_LOGI(GATTS_TABLE_TAG, "Stop adv successfully\n");
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
            break;
        case ESP_GAP_BLE_PASSKEY_REQ_EVT:                           /* passkey request event */
               ESP_LOGI(GATTS_TABLE_TAG, "ESP_GAP_BLE_PASSKEY_REQ_EVT");
               /* Call the following function to input the passkey which is displayed on the remote device */
               //esp_ble_passkey_reply(heart_rate_profile_tab[HEART_PROFILE_APP_IDX].remote_bda, true, 0x00);
               break;
           case ESP_GAP_BLE_OOB_REQ_EVT: {
               ESP_LOGI(GATTS_TABLE_TAG, "ESP_GAP_BLE_OOB_REQ_EVT");
               uint8_t tk[16] = {1}; //If you paired with OOB, both devices need to use the same tk
               esp_ble_oob_req_reply(param->ble_security.ble_req.bd_addr, tk, sizeof(tk));
               break;
           }
           case ESP_GAP_BLE_LOCAL_IR_EVT:                               /* BLE local IR event */
               ESP_LOGI(GATTS_TABLE_TAG, "ESP_GAP_BLE_LOCAL_IR_EVT");
               break;
           case ESP_GAP_BLE_LOCAL_ER_EVT:                               /* BLE local ER event */
               ESP_LOGI(GATTS_TABLE_TAG, "ESP_GAP_BLE_LOCAL_ER_EVT");
               break;
           case ESP_GAP_BLE_NC_REQ_EVT:
               /* The app will receive this evt when the IO has DisplayYesNO capability and the peer device IO also has DisplayYesNo capability.
               show the passkey number to the user to confirm it with the number displayed by peer device. */
               esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, true);
               ESP_LOGI(GATTS_TABLE_TAG, "ESP_GAP_BLE_NC_REQ_EVT, the passkey Notify number:%" PRIu32, param->ble_security.key_notif.passkey);
               break;
           case ESP_GAP_BLE_SEC_REQ_EVT:
               /* send the positive(true) security response to the peer device to accept the security request.
               If not accept the security request, should send the security response with negative(false) accept value*/
               esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
               break;
           case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:  ///the app will receive this evt when the IO  has Output capability and the peer device IO has Input capability.
               ///show the passkey number to the user to input it in the peer device.
               ESP_LOGI(GATTS_TABLE_TAG, "The passkey Notify number:%06" PRIu32, param->ble_security.key_notif.passkey);
               break;
           case ESP_GAP_BLE_KEY_EVT:
               //shows the ble key info share with peer device to the user.
               ESP_LOGI(GATTS_TABLE_TAG, "key type = %s", esp_key_type_to_str(param->ble_security.ble_key.key_type));
               break;
           case ESP_GAP_BLE_AUTH_CMPL_EVT: {
                   esp_bd_addr_t bd_addr;
                   memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
                   ESP_LOGI(GATTS_TABLE_TAG, "remote BD_ADDR: %02x:%02x:%02x:%02x:%02x:%02x",
                           bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
                   ESP_LOGI(GATTS_TABLE_TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
                   ESP_LOGI(GATTS_TABLE_TAG, "pair status = %s",param->ble_security.auth_cmpl.success ? "success" : "fail");

                   if(!param->ble_security.auth_cmpl.success) {
                       ESP_LOGI(GATTS_TABLE_TAG, "fail reason = 0x%x",param->ble_security.auth_cmpl.fail_reason);
                       ESP_LOGI(GATTS_TABLE_TAG, "key present = %d", param->ble_security.auth_cmpl.key_present);
                       pair_status = 0;

                       // Don't disconnect on pairing failure, just log it
                       ESP_LOGW(GATTS_TABLE_TAG, "Pairing failed but continuing with connection");
                   } else {
                       ESP_LOGI(GATTS_TABLE_TAG, "auth mode = %s",esp_auth_req_to_str(param->ble_security.auth_cmpl.auth_mode));
                       pair_status = 1;
                       ESP_LOGI(GATTS_TABLE_TAG, "Pairing successful");
                   }
                   show_bonded_devices();
                   break;
           }
           case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT: {
                   ESP_LOGD(GATTS_TABLE_TAG, "ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT status = %d", param->remove_bond_dev_cmpl.status);
                   ESP_LOGI(GATTS_TABLE_TAG, "ESP_GAP_BLE_REMOVE_BOND_DEV");
                   ESP_LOGI(GATTS_TABLE_TAG, "-----ESP_GAP_BLE_REMOVE_BOND_DEV----");
                   esp_log_buffer_hex(GATTS_TABLE_TAG, (void *)param->remove_bond_dev_cmpl.bd_addr, sizeof(esp_bd_addr_t));
                   ESP_LOGI(GATTS_TABLE_TAG, "------------------------------------");
                   break;
               }
           case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
                   if (param->local_privacy_cmpl.status != ESP_BT_STATUS_SUCCESS){
                       ESP_LOGE(GATTS_TABLE_TAG, "config local privacy failed, error status = %x", param->local_privacy_cmpl.status);
                   }else{
                       ESP_LOGI(GATTS_TABLE_TAG, "Local privacy configured successfully");
                       esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
                       if (ret){
                           ESP_LOGE(GATTS_TABLE_TAG, "config adv data failed, error code = %x", ret);
                       }else{
                           ESP_LOGI(GATTS_TABLE_TAG, "Advertising data configured successfully");
                           adv_config_done |= adv_config_flag;
                       }
                       ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
                       if (ret){
                           ESP_LOGE(GATTS_TABLE_TAG, "config scan response data failed, error code = %x", ret);
                       }else{
                           ESP_LOGI(GATTS_TABLE_TAG, "Scan response data configured successfully");
                           adv_config_done |= scan_rsp_config_flag;
                       }
                   }
                   break;
        default:
            break;
    }
}
void ble_client_send(char *data) {
	cJSON *root   = cJSON_Parse(data);
	char *jsonStr = cJSON_Print(root);
#if DEBUG_PRINT_EN
	printf("\njson data = %s\n", jsonStr);
#endif
	uint16_t len = strlen(jsonStr);
	printf("\nlen = %d\n", len);
	esp_err_t err = -1;

	err = esp_ble_gatts_send_indicate(interface_type, conn_id,
			heart_rate_handle_table[IDX_CHAR_VAL_B], len, (uint8_t*) jsonStr,
			false);
	if (err == ESP_OK) {
		printf("\nsend data to client: %s\n", data);
	} else {
		printf("\nFailure sending: %s, error: %s\n", data, esp_err_to_name(err));
	}
	cJSON_Delete(root);
	free(jsonStr);
}

void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf){
        esp_log_buffer_hex(GATTS_TABLE_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }else{
        ESP_LOGI(GATTS_TABLE_TAG,"ESP_GATT_PREP_WRITE_CANCEL");
    }

   printf("data recived:%s\n",prepare_write_env->prepare_buf);
    snprintf(data_validation, sizeof(data_validation), "%s",prepare_write_env->prepare_buf);
   printf("total data len:%d",prepare_write_env->prepare_len);
    data_validation[prepare_write_env->prepare_len]='\0';
#if DEBUG_PRINT_EN
    printf("\n exec_write_event Data received :%s\nlen : %d\n",data_validation,prepare_write_env->prepare_len);
#endif
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}

void example_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
//	ESP_LOGI(GATTS_TABLE_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);
    esp_gatt_status_t status = ESP_GATT_OK;
    if (prepare_write_env->prepare_buf == NULL) {
        prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
        prepare_write_env->prepare_len = 0;
        if (prepare_write_env->prepare_buf == NULL) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s, Gatt_server prep no mem", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    } else {
        if(param->write.offset > PREPARE_BUF_MAX_SIZE) {
            status = ESP_GATT_INVALID_OFFSET;
        } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
            status = ESP_GATT_INVALID_ATTR_LEN;
        }
    }
    /*send response when param->write.need_rsp is true */
    if (param->write.need_rsp){
        esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (gatt_rsp != NULL){
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK){
               ESP_LOGE(GATTS_TABLE_TAG, "Send response error");
            }
            free(gatt_rsp);
        }else{
            ESP_LOGE(GATTS_TABLE_TAG, "%s, malloc failed", __func__);
        }
    }
    if (status != ESP_GATT_OK){
        return;
    }
    memcpy(prepare_write_env->prepare_buf + param->write.offset,
           param->write.value,
           param->write.len);
	printf("\n----%s----",param->write.value);
    prepare_write_env->prepare_len += param->write.len;
}

// Task to handle WiFi scanning for BLE callbacks
void wifi_scan_callback_task(void *pvParameters) {
    scan_task_params_t *params = (scan_task_params_t *)pvParameters;
    vTaskDelay(100 / portTICK_PERIOD_MS);

    char *response = malloc(1000);
    if (!response) {
        ESP_LOGE(TAG, "Failed to allocate memory for response");
        free(params);
        vTaskDelete(NULL);
        return;
    }

    // Make sure we're not in a connecting state before scanning
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    bool is_connecting = !(bits & WIFI_CONNECTED_BIT) && !(bits & WIFI_FAIL_BIT);

    if (is_connecting) {
        // If we're in connecting state, set the FAIL bit to allow scanning
        ESP_LOGW(TAG, "WiFi was in connecting state, resetting state to allow scanning");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        vTaskDelay(100 / portTICK_PERIOD_MS); // Small delay to let state change take effect
    }

    memset(response, 0, 1000);
    scan_wifi_networks(response);

    printf("\nScan Response:\n%s\n", response);
    size_t resp_len = strlen(response);
    ESP_LOGI(TAG, "Response length: %d", resp_len);

    if (resp_len > 0 && resp_len < 1000) {
        int resp = esp_ble_gatts_send_indicate(
            params->gatts_if,
            params->conn_id,
            heart_rate_handle_table[IDX_CHAR_VAL_B],
            resp_len,
            (uint8_t *)response,
            false
        );
        printf("\nBLE response: %d\n", resp);
        if (resp != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send BLE response: %d", resp);
        }
    } else {
        ESP_LOGE(TAG, "Invalid response length");
    }

    // Always set the FAIL bit after scanning to ensure we're not stuck in connecting state
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

    free(response);
    free(params);
    vTaskDelete(NULL);
}

void get_wifi_credentials_from_app(const cJSON *ssid, const cJSON *password, const cJSON *device_id) {
    if (ssid && password && ssid->valuestring && password->valuestring) {
        // Free previous credentials if they exist
        if (wifi_credentials.ssid != NULL) {
            free(wifi_credentials.ssid);
            wifi_credentials.ssid = NULL;
        }
        if (wifi_credentials.password != NULL) {
            free(wifi_credentials.password);
            wifi_credentials.password = NULL;
        }
        if (wifi_credentials.device_id != NULL) {
            free(wifi_credentials.device_id);
            wifi_credentials.device_id = NULL;
        }


        // Allocate and copy values into wifi_credentials structure
        wifi_credentials.ssid = malloc(strlen(ssid->valuestring) + 1);
        wifi_credentials.password = malloc(strlen(password->valuestring) + 1);
        wifi_credentials.device_id = malloc(strlen(device_id->valuestring) + 1);

        if (wifi_credentials.ssid && wifi_credentials.password) {
            strcpy((char *)wifi_credentials.ssid, ssid->valuestring);
            strcpy((char *)wifi_credentials.password, password->valuestring);
            strcpy((char *)wifi_credentials.device_id, device_id->valuestring);
            ESP_LOGI(TAG, "Received WiFi credentials - SSID: %s", wifi_credentials.ssid);
            
            wifi_creds_ready = true;
            // Connect to WiFi with new credentials
            connect_wifi_with_new_credentials_task();
        } else {
            ESP_LOGE(TAG, "Memory allocation failed for WiFi credentials");
            ble_client_send("{\"wifi_status\":\"error\",\"message\":\"Memory allocation failed\"}");
        }
    } else {
        ESP_LOGE(TAG, "Missing or invalid SSID or PASSWORD in JSON");
        ble_client_send("{\"wifi_status\":\"error\",\"message\":\"Missing SSID or PASSWORD\"}");
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
			printf("\n ----ESP_GATTS_REG_EVT-----");
            ESP_LOGI(GATTS_TABLE_TAG, "Setting BLE device name to: %s", BLE_DEVICE_NAME);
            esp_ble_gap_set_device_name((const char *)BLE_DEVICE_NAME);
            esp_ble_gap_config_local_privacy(true);
            esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, SVC_INST_ID);
            break;
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONNECT_EVT, conn_id = %d", param->connect.conn_id);
            interface_type = gatts_if;
            conn_id = param->connect.conn_id;
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Client disconnected");
            is_authenticated = false; // Reset authentication on disconnect
            conn_id = 0;
            // esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GATTS_WRITE_EVT:
        printf("\n ****************ESP_GATTS_WRITE_EVT*******************");
        if (!param->write.is_prep){

            snprintf(data_validation, sizeof(data_validation), "%s",param->write.value);
            printf("Data received :%s\nlen : %d\n",data_validation,param->write.len);
#if DEBUG_PRINT_EN
             printf("Data received :%s\nlen : %d\n",data_validation,param->write.len);
#endif
             if(heart_rate_handle_table[IDX_CHAR_VAL_A] == param->write.handle){
               	ESP_LOGI(GATTS_TABLE_TAG, "WRITE IDX_CHAR_VAL_A");
                if((strcmp(data_validation,AUTH_KEY)==0) ){
                    printf("\nAUTH DONE\n");
                }
                // Check if this is a JSON command
                else if(data_validation[0] == '{') {
                    // Parse JSON
                    cJSON *root = cJSON_Parse(data_validation);
                    if (root != NULL) {
                        cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
                        if (cmd != NULL && cJSON_IsString(cmd) && strcmp(cmd->valuestring, "get_deviceid") == 0) {
                            printf("\nDevice ID To App : %s\n",DEVICE_ID);
                            char device_id_json[100];
                            sprintf(device_id_json, "{\"device_id\":\"%s\",\"device_type\":\"AIOS_1\"}", DEVICE_ID);
                            ble_client_send(device_id_json);
                        }
                        cJSON_Delete(root);
                    }
                }
                else{
                    printf("\n!!! Authentication failed : disconnecting from client !!!\n");
                }
            }
            else if(heart_rate_handle_table[IDX_CHAR_VAL_B] == param->write.handle){
               	ESP_LOGI(GATTS_TABLE_TAG, "WRITE IDX_CHAR_VAL_B");
                ESP_LOGI(TAG, "param->write.conn_id = %d", param->write.conn_id);

                // Parse JSON to validate the request
                cJSON *root = cJSON_Parse((const char *)data_validation);
                if (root == NULL) {
                    printf("JSON Parse Error!\n");
                    return;
                }

                // Check if this is a WiFi scan request
                cJSON *cmd_type = cJSON_GetObjectItem(root, "cmd_type");
                if (cmd_type != NULL && cJSON_IsString(cmd_type)) {
                    if (strcmp(cmd_type->valuestring, "scan_list") == 0) {
                        // Create a task to handle WiFi scanning to avoid conflicts with BLE
                        scan_task_params_t *params = malloc(sizeof(scan_task_params_t));
                        if (params != NULL) {
                            params->gatts_if = gatts_if;
                            params->conn_id = param->write.conn_id;

                            xTaskCreate(
                                wifi_scan_callback_task,
                                "wifi_scan_task",
                                8192,
                                (void*)params,  // Pass the parameter structure
                                5,
                                NULL
                            );
                        } else {
                            ESP_LOGE(TAG, "Failed to allocate memory for task parameters");
                        }
                    } else if (strcmp(cmd_type->valuestring, "verify_device") == 0) {
                        // Handle device ID verification
                        ESP_LOGI(TAG, "Device id Verification");
                        cJSON *device_id = cJSON_GetObjectItem(root, "device_id");
                        if (device_id != NULL && cJSON_IsString(device_id)) {
                            // Check if the device ID matches
                            bool is_verified = check_device_id(device_id->valuestring);

                            // Send verification response
                            send_device_verification_response(is_verified);

                            ESP_LOGI(TAG, "Device verification request processed");
                        } else {
                            ESP_LOGE(TAG, "Missing or invalid device_id in verification request");
                            send_device_verification_response(false);
                        }
                    } else if (strcmp(cmd_type->valuestring, "connect") == 0) {
                        // Handle WiFi credentials if present
                        ESP_LOGI(TAG, "WiFi Connection Request");
                        cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
                        cJSON *password = cJSON_GetObjectItem(root, "password");
                        cJSON *device_id = cJSON_GetObjectItem(root, "device_id");

                        if (ssid != NULL && password != NULL) {
                            printf("SSID: %s\n", ssid->valuestring);
                            printf("PASSWORD: %s\n", password->valuestring);
                            printf("Device ID: %s\n", device_id->valuestring);
                            get_wifi_credentials_from_app(ssid, password, device_id);
                            
                        } else {
                            printf("Missing SSID or PASSWORD in JSON\n");
                            ble_client_send("{\"wifi_status\":\"error\",\"message\":\"Missing SSID or PASSWORD\"}");
                        }
                    } else {
                        ESP_LOGW(TAG, "Unknown command type: %s", cmd_type->valuestring);
                    }
                } else {
                    // Handle WiFi credentials if present (for backward compatibility)
                    cJSON *ssid = cJSON_GetObjectItem(root, "SSID");
                    cJSON *password = cJSON_GetObjectItem(root, "PASSWORD");
                    cJSON *device_id = cJSON_GetObjectItem(root, "device_id");
                    if (ssid != NULL && password != NULL) {
                        get_wifi_credentials_from_app(ssid, password, device_id);
                    }
                }

                cJSON_Delete(root);
            }
            else if (heart_rate_handle_table[IDX_CHAR_CFG_A] == param->write.handle && param->write.len == 2){
//					ESP_LOGI(GATTS_TABLE_TAG, "NOTIFY DATA");
                uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                if (descr_value == 0x0001){
//                        ESP_LOGI(GATTS_TABLE_TAG, "notify enable A");
                    uint8_t notify_data[15];
                    for (int i = 0; i < sizeof(notify_data); ++i)
                    {
                        notify_data[i] = i % 0xff;
                    }
                    //the size of notify_data[] need less than MTU size
                    esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, heart_rate_handle_table[IDX_CHAR_VAL_A],
                                            sizeof(notify_data), notify_data, false);
                }else if (descr_value == 0x0002){
//                        ESP_LOGI(GATTS_TABLE_TAG, "indicate enable A");
                    uint8_t indicate_data[15];
                    for (int i = 0; i < sizeof(indicate_data); ++i)
                    {
                        indicate_data[i] = i % 0xff;
                    }
                    //the size of indicate_data[] need less than MTU size
                    esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, heart_rate_handle_table[IDX_CHAR_VAL_A],
                                        sizeof(indicate_data), indicate_data, true);
                }
                else if (descr_value == 0x0000){
//                        ESP_LOGI(GATTS_TABLE_TAG, "notify/indicate disable A ");
                }else{
                    ESP_LOGE(GATTS_TABLE_TAG, "unknown descr value");
                    esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);
                }

            }else if (heart_rate_handle_table[IDX_CHAR_CFG_B] == param->write.handle && param->write.len == 2){
                uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                if (descr_value == 0x0001){
//                        ESP_LOGI(GATTS_TABLE_TAG, "notify enable B");
                    uint8_t notify_data[15];
                    for (int i = 0; i < sizeof(notify_data); ++i)
                    {
                        notify_data[i] = i % 0xff;
                    }
                    //the size of notify_data[] need less than MTU size
                    esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, heart_rate_handle_table[IDX_CHAR_VAL_B],
                                            sizeof(notify_data), notify_data, false);
                }else if (descr_value == 0x0002){
//                        ESP_LOGI(GATTS_TABLE_TAG, "indicate enable B");
                    uint8_t indicate_data[15];
                    for (int i = 0; i < sizeof(indicate_data); ++i)
                    {
                        indicate_data[i] = i % 0xff;
                    }
                    //the size of indicate_data[] need less than MTU size
                    esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, heart_rate_handle_table[IDX_CHAR_VAL_B],
                                        sizeof(indicate_data), indicate_data, true);
                }
                else if (descr_value == 0x0000){
//                        ESP_LOGI(GATTS_TABLE_TAG, "notify/indicate disable B ");
                }else{
                    ESP_LOGE(GATTS_TABLE_TAG, "unknown descr value");
                    esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);
                }
            }
            else{
                ESP_LOGE(GATTS_TABLE_TAG, "UN Handled Data");
            }
            /* send response when param->write.need_rsp is true*/
            if (param->write.need_rsp){
                printf("send response");
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            }
        }
        else{
            /* handle prepare write */
            example_prepare_write_event_env(gatts_if, &prepare_write_env_b, param);
        }
        break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
            printf("Full received data: %.*s\n", prepare_write_env_b.prepare_len, prepare_write_env_b.prepare_buf);
            // Parse JSON
            cJSON *root = cJSON_Parse((const char *)prepare_write_env_b.prepare_buf);
            if (root == NULL) {
                printf("JSON Parse Error!\n");
                return;
            }

            cJSON *ssid = cJSON_GetObjectItem(root, "SSID");
            cJSON *password = cJSON_GetObjectItem(root, "PASSWORD");
            cJSON *device_id = cJSON_GetObjectItem(root, "device_id");

            if (ssid && password) {
                get_wifi_credentials_from_app(ssid, password, device_id);
            }
            cJSON_Delete(root);
            example_exec_write_event_env(&prepare_write_env_b, param);
            break;
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_CONF_EVT:
            // ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONF_EVT, status = %d, attr_handle %d", param->conf.status, param->conf.handle);
            break;
        case ESP_GATTS_START_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
            break;
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:{
            if (param->add_attr_tab.status != ESP_GATT_OK){
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
            }
            else if (param->add_attr_tab.num_handle != HRS_IDX_NB){
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table abnormally, num_handle (%d) \
                        doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, HRS_IDX_NB);
            }
            else {
//              ESP_LOGI(GATTS_TABLE_TAG, "create attribute table successfully, the number handle = %d\n",param->add_attr_tab.num_handle);
                memcpy(heart_rate_handle_table, param->add_attr_tab.handles, sizeof(heart_rate_handle_table));
                esp_ble_gatts_start_service(heart_rate_handle_table[IDX_SVC]);
            }
            break;
        }
        case ESP_GATTS_STOP_EVT:
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        case ESP_GATTS_UNREG_EVT:
        case ESP_GATTS_DELETE_EVT:
        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{

    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            heart_rate_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGE(GATTS_TABLE_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == heart_rate_profile_tab[idx].gatts_if) {
                if (heart_rate_profile_tab[idx].gatts_cb) {
                    heart_rate_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

// Function to initialize Bluetooth
esp_err_t bluetooth_init(void)
{
    esp_err_t ret;    
    ESP_LOGI(TAG, "Initializing Bluetooth for ESP32-C3");
    
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Release classic BT memory failed: %s (continuing...)", esp_err_to_name(ret));
    }

    // Initialize Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Initialize controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "Enable controller failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Init bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Enable bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "Register GATTS callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "Register GAP callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register your GATT service
    ret = esp_ble_gatts_app_register(0x55);
    if (ret) {
        ESP_LOGE(TAG, "Register GATTS application failed: %s", esp_err_to_name(ret));
        return ret;
    }


     /* set the security iocap & auth_req & key size & init key response key parameters to the stack*/
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;    
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;        
    uint8_t key_size = 16;      
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t auth_option = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_DISABLE;
    uint8_t oob_support = ESP_BLE_OOB_DISABLE;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &auth_option, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oob_support, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    ESP_LOGI(TAG, "Bluetooth initialized successfully");

    return ESP_OK;
}

void bluetooth_start_advertising(void) {
    esp_ble_gap_start_advertising(&adv_params);
    ESP_LOGI(TAG, "BLE advertising started");
}

void bluetooth_stop_advertising(void) {
    esp_ble_gap_stop_advertising();
    ESP_LOGI(TAG, "BLE advertising stopped");
}

// Task to connect to WiFi with new credentials received via BLE
void connect_wifi_with_new_credentials_task(void) {
    ESP_LOGI(TAG, "Connecting to WiFi with new credentials...");

    // Reset event bits before attempting connection
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // Stop WiFi completely
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi stop failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for WiFi to stop

    // Configure WiFi with new credentials
    wifi_config_t wifi_config = {0};

    // Copy credentials safely
    if (wifi_credentials.ssid && wifi_credentials.password) {
        strlcpy((char *)wifi_config.sta.ssid, (const char *)wifi_credentials.ssid, sizeof(wifi_config.sta.ssid));
        strlcpy((char *)wifi_config.sta.password, (const char *)wifi_credentials.password, sizeof(wifi_config.sta.password));

        ESP_LOGI(TAG, "*****New WiFi credentials: SSID=%s, Password=%s", wifi_config.sta.ssid, wifi_config.sta.password);
    } else {
        ESP_LOGE(TAG, "WiFi credentials are NULL");
        ble_client_send("{\"wifi_status\":\"error\",\"message\":\"Invalid WiFi credentials\"}");
        return;
    }

    // Set authentication mode and other WiFi parameters
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // Set WiFi configuration
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set config failed: %s", esp_err_to_name(err));
        ble_client_send("{\"wifi_status\":\"error\",\"message\":\"Failed to set WiFi config\"}");
        return;
    }

    // Restart WiFi
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(err));
        ble_client_send("{\"wifi_status\":\"error\",\"message\":\"Failed to start WiFi\"}");
        return;
    }

    // Wait for WiFi to fully initialize
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Send acknowledgment that credentials were received and we're attempting to connect
    ble_client_send("{\"wifi_status\":\"credentials_received\"}");

    // Connect to WiFi - handle errors gracefully
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "First WiFi connect attempt failed: %s - will retry after delay", esp_err_to_name(err));

        // Wait a bit longer and try again
        vTaskDelay(pdMS_TO_TICKS(2000));

        err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Second WiFi connect attempt failed: %s - will rely on auto-reconnect", esp_err_to_name(err));
            // Don't return here - the event handler will retry the connection
        } else {
            ESP_LOGI(TAG, "Second WiFi connect attempt successful");
        }
    } else {
        ESP_LOGI(TAG, "First WiFi connect attempt successful");
    }

    // Wait for connection or failure (timeout after 30 seconds)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(30000));

    // Check connection result
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s", wifi_credentials.ssid);

        // Wait a bit longer to ensure IP is assigned (up to 5 seconds)
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Get IP Address after WiFi is connected
        esp_netif_ip_info_t ip_info = {0};  // Initialize to zeros
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            esp_netif_get_ip_info(netif, &ip_info);
            ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&ip_info.ip));

            // Only save credentials and send connected status if we actually have a valid IP address
            if (ip_info.ip.addr != 0) {
                // Connection successful - NOW we can save the credentials to NVS
                ESP_LOGI(TAG, "WiFi connection successful with valid IP address - saving credentials to NVS");
                store_wifi_credentials_to_nvs();  // Save credentials after successful connection

                // Create JSON response with connection status
                char response[100];
                snprintf(response, sizeof(response), "{\"wifi_status\":\"connected\"}");

                // Send success message to the BLE client
                ble_client_send(response);
                
                // Stop BLE advertising after successful WiFi connection
                esp_ble_gap_stop_advertising();
                ESP_LOGI(TAG, "BLE advertising stopped after WiFi connection");
            } else {
                ESP_LOGE(TAG, "Connected but no valid IP received");
                ble_client_send("{\"wifi_status\":\"failed\",\"message\":\"Connected but no IP assigned\"}");
                // Disconnect from WiFi since we didn't get a valid IP
                esp_wifi_disconnect();
            }

        } else {
            ESP_LOGE(TAG, "Failed to get netif handle");
            ble_client_send("{\"wifi_status\":\"error\",\"message\":\"Failed to get IP\"}");
        }
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", wifi_credentials.ssid);
        ble_client_send("{\"wifi_status\":\"failed\"}");

        // Clear credentials from memory since they failed to connect
        // This prevents them from being saved to NVS in the future
        ESP_LOGI(TAG, "Clearing invalid WiFi credentials from memory");
        if (wifi_credentials.ssid) {
            free(wifi_credentials.ssid);
            wifi_credentials.ssid = NULL;
        }
        if (wifi_credentials.password) {
            free(wifi_credentials.password);
            wifi_credentials.password = NULL;
        }
        if(wifi_credentials.device_id) {
            free(wifi_credentials.device_id);
            wifi_credentials.device_id = NULL;
        }
    } else {
        ESP_LOGI(TAG, "Connection timeout for SSID:%s", wifi_credentials.ssid);
        ble_client_send("{\"wifi_status\":\"timeout\",\"message\":\"Connection timeout\"}");
    }
}

bool check_device_id(const char* received_device_id) {
    // Your expected device ID
    char expected_device_id[30] = {0};
    strncpy(expected_device_id, BLE_DEVICE_NAME, sizeof(expected_device_id) - 1);
    expected_device_id[sizeof(expected_device_id) - 1] = '\0';

    // Compare the received device ID with the expected one
    if (strcmp(received_device_id, expected_device_id) == 0) {
        ESP_LOGI(TAG, "Device ID matched: %s", received_device_id);
        return true;
    } else {
        ESP_LOGE(TAG, "Device ID mismatch. Received: %s, Expected: %s",
                 received_device_id, expected_device_id);
        return false;
    }
}

void send_device_verification_response(bool is_verified) {
    char response[100];

    if (is_verified) {
        snprintf(response, sizeof(response),
                 "{\"status\":\"success\",\"message\":\"Device verified\"}");
    } else {
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"message\":\"Unknown device\"}");
    }
    // Send the response via BLE
    ble_client_send(response);
    ESP_LOGI(TAG, "Sent device verification response: %s", response);
}
