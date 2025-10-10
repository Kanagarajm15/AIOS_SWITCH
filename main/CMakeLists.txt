idf_component_register(SRCS "mqtt_client.c" "nvs.c" "wifi.c" "bluetooth.c" "led.c" "main.c" "switch_controller.c"
                    INCLUDE_DIRS "." "include"
                    REQUIRES nvs_flash esp_http_server esp_netif mqtt bt driver json esp_http_client)