#ifndef LED_H
#define LED_H

#include "led_strip.h"


// RGB LED pin definition for ESP32-C3 DevKit (GPIO 8 - RGB LED)
#define RGB_LED_PIN    8

static led_strip_handle_t led_strip;

// Function declarations
void rgb_led_init(void);
void rgb_led_set_red(void);
void rgb_led_set_green(void);
void rgb_led_set_blue(void);
void rgb_led_set_orange(void);
void rgb_led_set_purple(void);
void rgb_led_set_off(void);

#endif /* LED_H */
