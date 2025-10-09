#include "esp_log.h"
#include "led.h"
#include "led_strip.h"

static const char *TAG = "LED";

// RGB LED control functions
void rgb_led_init(void) {
    ESP_LOGI(TAG, "Initializing RGB LED strip on GPIO %d", RGB_LED_PIN);
    
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_PIN,
        .max_leds = 1, // Single LED on board
    };
    
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#else
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#endif
    
    /* Set LED off initially */
    led_strip_clear(led_strip);
}

void rgb_led_set_red(void) {
    /* Set the LED to red color for WiFi disconnected */
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 255, 0, 0)); // Red: R=255, G=0, B=0
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    ESP_LOGI(TAG, "RGB LED set to RED (WiFi disconnected)");
}

void rgb_led_set_green(void) {
    /* Set the LED to green color for WiFi connected */
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 255, 0)); // Green: R=0, G=255, B=0
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    ESP_LOGI(TAG, "RGB LED set to GREEN (WiFi connected)");
}

void rgb_led_set_blue(void) {
    /* Set the LED to blue color */
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, 255)); // Blue: R=0, G=0, B=255
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    ESP_LOGI(TAG, "RGB LED set to BLUE");
}

void rgb_led_set_orange(void) {
    /* Set the LED to orange color */
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 255, 165, 0)); // Orange: R=255, G=165, B=0
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    ESP_LOGI(TAG, "RGB LED set to ORANGE");
}

void rgb_led_set_purple(void) {
    /* Set the LED to purple/magenta color */
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 255, 0, 255)); // Purple: R=255, G=0, B=255
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    ESP_LOGI(TAG, "RGB LED set to PURPLE");
}
void rgb_led_set_off(void) {
    /* Turn off the LED */
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}
