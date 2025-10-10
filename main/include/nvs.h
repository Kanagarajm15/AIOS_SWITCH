#ifndef NVS_H
#define NVS_H

#include <stddef.h>
#include "esp_err.h"

void nvs_read_wifi_credentials(char *read_ssid, char *read_password, char *read_device_id);

void store_wifi_credentials_to_nvs(void);

void nvs_init(void);

#endif /* NVS_H */