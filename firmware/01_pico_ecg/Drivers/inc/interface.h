#ifndef INTERFACE_H
#define INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "u8g2.h"

// Access to the global u8g2 instance
extern u8g2_t u8g2;

// Initialization
void interface_init(void);
void interface_splash_animation(void);
void interface_show_info_screen(void);
void interface_update(void);

// LED Control
void interface_led_set(bool on);
void interface_led_toggle(void);

// Buzzer Control
void interface_buzzer_beep(uint32_t frequency_hz, uint32_t duration_ms);
void interface_buzzer_beep_nonblocking(uint32_t frequency_hz, uint32_t duration_ms);
void interface_trigger_heartbeat(void);

// Encoder / Input
int32_t interface_get_encoder_delta(void);
bool interface_get_button_pressed(void);
bool interface_get_button_long_pressed(void);

// Battery and System Power
float interface_get_battery_voltage(void);
bool interface_is_usb_connected(void);

#endif // INTERFACE_H
