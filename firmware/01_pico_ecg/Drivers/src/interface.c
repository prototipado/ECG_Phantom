#include "interface.h"
#include "animation.h"
#include "menu.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/adc.h"

// ==================== Pin Definitions ====================
// OLED
#define SPI_PORT spi0
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_DC   16
#define PIN_RST  17

// Encoder
#define PIN_ENCODER_DATA 21
#define PIN_ENCODER_CLK  22
#define PIN_ENCODER_BTN  20

// Peripherals
#define PIN_LED    5
#define PIN_BUZZER 15
#define PIN_ONBOARD_LED 25

// Battery and System
#define PIN_BATT_ADC 29  // ADC3
#define PIN_USB_DET  24

// ==================== Global Variables ====================
u8g2_t u8g2;

// Encoder State
static volatile int32_t encoder_count = 0;
static volatile int32_t last_encoder_read = 0;
static volatile uint8_t encoder_ab_last = 3; // Last quadrature state (CLK<<1)|DATA, 3 = both high (rest)
static volatile uint8_t encoder_cycle_edges = 0; // Transitions accumulated in current detent cycle
static volatile int8_t encoder_cycle_dir = 0;    // Running cycle direction (+1/-1), 0 = idle
static volatile bool button_pressed = false;
static volatile bool button_long_pressed = false;
static volatile bool button_released_event = false; // Internal for debouncing/release logic if needed
static uint32_t button_press_time = 0;

// ==================== Private Functions ====================
static void gpio_callback(uint gpio, uint32_t events);
static void encoder_process(void);

static uint8_t u8g2_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_GPIO_DC:
            gpio_put(PIN_DC, arg_int);
            break;
        case U8X8_MSG_GPIO_RESET:
            gpio_put(PIN_RST, arg_int);
            break;
        case U8X8_MSG_DELAY_NANO:
            break;
        case U8X8_MSG_DELAY_10MICRO:
            sleep_us(10);
            break;
        case U8X8_MSG_DELAY_100NANO:
            break;
        case U8X8_MSG_DELAY_MILLI:
            sleep_ms(1);
            break;
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            break;
        default:
            return 0;
    }
    return 1;
}

static uint8_t u8g2_spi_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_BYTE_SEND:
            spi_write_blocking(SPI_PORT, (uint8_t *)arg_ptr, arg_int);
            break;
        case U8X8_MSG_BYTE_INIT:
            break;
        case U8X8_MSG_BYTE_SET_DC:
            gpio_put(PIN_DC, arg_int);
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            break;
        default:
            return 0;
    }
    return 1;
}

// ==================== Public Functions ====================

// Interface implementation starts from here (PWM init included below)

#include "hardware/pwm.h"
#include "hardware/clocks.h"

// ... (previous code)

void interface_init(void) {
    // --- LED ---
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);

    // --- Buzzer (PWM) ---
    gpio_set_function(PIN_BUZZER, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(PIN_BUZZER);
    pwm_set_enabled(slice_num, false); // Disabled at start

    // --- Display ---
    spi_init(SPI_PORT, 2000000); 
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    gpio_init(PIN_DC);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_put(PIN_DC, 0);

    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_RST, 1);

    // Initialize U8g2 (Try SH1106 if SSD1306 shows artifacts)
    u8g2_Setup_sh1106_128x64_noname_f(&u8g2, U8G2_R0, u8g2_spi_byte_cb, u8g2_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
    
    // --- Encoder ---
    gpio_init(PIN_ENCODER_CLK);
    gpio_set_dir(PIN_ENCODER_CLK, GPIO_IN);
    gpio_pull_up(PIN_ENCODER_CLK);

    gpio_init(PIN_ENCODER_DATA);
    gpio_set_dir(PIN_ENCODER_DATA, GPIO_IN);
    gpio_pull_up(PIN_ENCODER_DATA);

    gpio_init(PIN_ENCODER_BTN);
    gpio_set_dir(PIN_ENCODER_BTN, GPIO_IN);
    gpio_pull_up(PIN_ENCODER_BTN);

    gpio_set_irq_enabled_with_callback(PIN_ENCODER_BTN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
    gpio_set_irq_enabled(PIN_ENCODER_CLK, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(PIN_ENCODER_DATA, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    encoder_ab_last = (uint8_t)((gpio_get(PIN_ENCODER_CLK) << 1) | gpio_get(PIN_ENCODER_DATA));

    // --- Battery ADC ---
    adc_init();
    adc_gpio_init(PIN_BATT_ADC);
    
    // --- USB Detection ---
    // Onboard LED
    gpio_init(PIN_ONBOARD_LED);
    gpio_set_dir(PIN_ONBOARD_LED, GPIO_OUT);

    gpio_init(PIN_USB_DET);
    gpio_set_dir(PIN_USB_DET, GPIO_IN);
    gpio_pull_down(PIN_USB_DET); // Weak pull-down to ensure 0 when not connected
}

void interface_splash_animation(void) {
    for (int i = 0; i < ANIMATION_FRAMES; i++) {
        // Trigger melody notes at specific frames (Non-blocking requests)
        if (i == 0)  interface_buzzer_beep_nonblocking(440, 50); // A4
        if (i == 4)  interface_buzzer_beep_nonblocking(554, 50); // C#5
        if (i == 8)  interface_buzzer_beep_nonblocking(659, 50); // E5
        if (i == 12) interface_buzzer_beep_nonblocking(880, 80); // A5
        if (i == 14) interface_buzzer_beep_nonblocking(659, 50); // E5
        //if (i == 16) interface_buzzer_beep_nonblocking(880, 100); // A5
        u8g2_FirstPage(&u8g2);
        do {
            u8g2_SetBitmapMode(&u8g2, 1); // Transparent
            u8g2_DrawXBMP(&u8g2, 28, 0, 71, 64, epd_bitmap_allArray[i]);
        } while (u8g2_NextPage(&u8g2));
        
        // We must call interface_update if we want non-blocking beeps to work
        // during this blocking animation loop.
        interface_update();
        sleep_ms(50);
    }
}

void interface_show_info_screen(void) {
    u8g2_FirstPage(&u8g2);
    do {
        u8g2_SetFont(&u8g2, u8g2_font_9x15_tr);
        u8g2_DrawStr(&u8g2, 28, 15, "Open-ECG");
        u8g2_DrawStr(&u8g2, 23, 31, "Generator");
        u8g2_DrawStr(&u8g2, 32, 47, "FI-UNER");
        u8g2_DrawStr(&u8g2, 18, 63, "v1.1-2026");
    } while (u8g2_NextPage(&u8g2));
    sleep_ms(2000);
}

void interface_led_set(bool on) {
    gpio_put(PIN_LED, on);
}

void interface_led_toggle(void) {
    if (settings.led_enabled) {
        gpio_xor_mask(1u << PIN_LED);
    }else  {
        gpio_put(PIN_LED, 0);
    }
}

// Non-blocking buzzer state
static bool beep_active = false;
static absolute_time_t beep_end_time;

// Heartbeat state (visual + audible)
static volatile bool heartbeat_req = false;
static bool heartbeat_active = false;
static absolute_time_t heartbeat_end_time;

// USB Activity tracking
static absolute_time_t usb_activity_end_time;

// TinyUSB RX Callback - Called when data is received from host
void tud_cdc_rx_cb(uint8_t itf) {
    usb_activity_end_time = make_timeout_time_ms(30);
}

// TinyUSB TX Callback - Called when data host has read data sent by us
void tud_cdc_tx_complete_cb(uint8_t itf) {
    usb_activity_end_time = make_timeout_time_ms(30);
}

// Blocking beep (keep for legacy/startup calls)
void interface_buzzer_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    if (frequency_hz == 0 || !settings.buzzer_enabled) return;

    uint slice_num = pwm_gpio_to_slice_num(PIN_BUZZER);
    uint channel = pwm_gpio_to_channel(PIN_BUZZER);
    
    // PWM Config
    uint32_t clock_rate = clock_get_hz(clk_sys);
    uint32_t divider = 16; 
    uint32_t wrap = (clock_rate / divider) / frequency_hz - 1;

    pwm_set_clkdiv(slice_num, (float)divider);
    pwm_set_wrap(slice_num, (uint16_t)wrap);
    pwm_set_chan_level(slice_num, channel, (uint16_t)(wrap / 2)); 
    
    pwm_set_enabled(slice_num, true);
    sleep_ms(duration_ms);
    pwm_set_enabled(slice_num, false);
}

// Queue for buzzer requests (simple 1-level queue for now)
static volatile uint32_t beep_req_freq = 0;
static volatile uint32_t beep_req_dur = 0;

void interface_buzzer_beep_nonblocking(uint32_t frequency_hz, uint32_t duration_ms) {
    if (frequency_hz == 0 || !settings.buzzer_enabled) return;
    beep_req_freq = frequency_hz;
    beep_req_dur = duration_ms;
}

void interface_update(void) {
    // 1. Handle regular buzzer requests
    if (beep_active) {
        // Check if time to turn off
        if (absolute_time_diff_us(get_absolute_time(), beep_end_time) < 0) {
            uint slice_num = pwm_gpio_to_slice_num(PIN_BUZZER);
            pwm_set_enabled(slice_num, false);
            beep_active = false;
        }
    } else if (!heartbeat_active) {
        // Check for new regular request (only if no heartbeat is active)
        if (beep_req_freq > 0 && beep_req_dur > 0 && settings.buzzer_enabled) {
            uint32_t f = beep_req_freq;
            uint32_t d = beep_req_dur;
            // Clear request
            beep_req_freq = 0;
            
            // Start PWM
            uint slice_num = pwm_gpio_to_slice_num(PIN_BUZZER);
            uint channel = pwm_gpio_to_channel(PIN_BUZZER);
            
            uint32_t clock_rate = clock_get_hz(clk_sys);
            uint32_t divider = 16; 
            uint32_t wrap = (clock_rate / divider) / f - 1;

            pwm_set_clkdiv(slice_num, (float)divider);
            pwm_set_wrap(slice_num, (uint16_t)wrap);
            pwm_set_chan_level(slice_num, channel, (uint16_t)(wrap / 2)); 
            
            pwm_set_enabled(slice_num, true);
            
            beep_active = true;
            beep_end_time = make_timeout_time_ms(d);
        }
    }

    // 2. Handle Heartbeat requests (LED + Buzzer)
    if (heartbeat_req) {
        heartbeat_req = false;
        
        // Start Heartbeat indications
        if (settings.led_enabled) {
            gpio_put(PIN_LED, 1);
        }
        
        if (settings.buzzer_enabled && !beep_active) {
            uint slice_num = pwm_gpio_to_slice_num(PIN_BUZZER);
            uint channel = pwm_gpio_to_channel(PIN_BUZZER);
            uint32_t clock_rate = clock_get_hz(clk_sys);
            uint32_t divider = 16; 
            uint32_t wrap = (clock_rate / divider) / 1500 - 1; // 1.5kHz for heartbeat

            pwm_set_clkdiv(slice_num, (float)divider);
            pwm_set_wrap(slice_num, (uint16_t)wrap);
            pwm_set_chan_level(slice_num, channel, (uint16_t)(wrap / 2)); 
            pwm_set_enabled(slice_num, true);
        }
        
        heartbeat_active = true;
        heartbeat_end_time = make_timeout_time_ms(50); // 50ms pulse
    } else if (heartbeat_active) {
        if (absolute_time_diff_us(get_absolute_time(), heartbeat_end_time) < 0) {
            // Pulse finished
            gpio_put(PIN_LED, 0);
            
            // Only stop buzzer if it was the heartbeat buzzer
            if (!beep_active) {
                uint slice_num = pwm_gpio_to_slice_num(PIN_BUZZER);
                pwm_set_enabled(slice_num, false);
            }
            
            heartbeat_active = false;
        }
    }

    // 3. Handle USB activity LED (Onboard GPIO 25)
    bool usb_active = absolute_time_diff_us(get_absolute_time(), usb_activity_end_time) > 0;
    gpio_put(PIN_ONBOARD_LED, usb_active);
}

int32_t interface_get_encoder_delta(void) {
    int32_t delta = encoder_count - last_encoder_read;
    last_encoder_read = encoder_count;
    return delta;
}

bool interface_get_button_pressed(void) {
    if (button_pressed) {
        button_pressed = false;
        return true;
    }
    return false;
}

bool interface_get_button_long_pressed(void) {
    if (button_long_pressed) {
        button_long_pressed = false;
        return true;
    }
    return false;
}

void interface_trigger_heartbeat(void) {
    heartbeat_req = true;
}

float interface_get_battery_voltage(void) {
    // Select ADC input 3 (GPIO 29)
    adc_select_input(3);
    
    // Read raw value (12-bit)
    uint16_t raw = adc_read();
    
    // Convert to voltage at pin: raw * 3.3V / 4096
    float pin_voltage = (float)raw * 3.3f / 4096.0f;
    
    // System voltage is divided by 3 (according to user request)
    // Calibration: 
    // Case 1: USB Connected (USB power bypasses path diodes)
    //   actual=4.48V shown=4.6V -> factor = 4.48/4.6 = 0.9739
    // Case 2: Battery Only (Voltage drop in path diodes/regulator)
    //   actual=4.13V shown=3.4V (with prev factor) -> factor = 0.9739 * (4.13/3.4) = 1.183
    float factor = interface_is_usb_connected() ? 0.9739f : 1.183f;
    
    return pin_voltage * 3.0f * factor;
}

bool interface_is_usb_connected(void) {
    return gpio_get(PIN_USB_DET);
}

// ... (gpio_callback refactored)
// Quadrature decoder lookup table: [previous][new] where state = (CLK<<1)|DATA.
// +1 = one valid step CW, -1 = CCW, 0 = reversed/bounce (ignored).
// Every edge of both channels is validated, so contact bounce cancels itself
// and false direction reversals are filtered out.
static const int8_t ENC_TRANS[4][4] = {
    {  0,  1, -1,  0 }, // prev 00 -> 01:+1, 10:-1
    { -1,  0,  0,  1 }, // prev 01 -> 00:-1, 11:+1
    {  1,  0,  0, -1 }, // prev 10 -> 00:+1, 11:-1
    {  0, -1,  1,  0 }, // prev 11 -> 01:-1, 10:+1
};

static void encoder_process(void) {
    uint8_t ab = (uint8_t)((gpio_get(PIN_ENCODER_CLK) << 1) | gpio_get(PIN_ENCODER_DATA));
    if (ab == encoder_ab_last) return;
    uint8_t prev = encoder_ab_last;
    encoder_ab_last = ab;

    int8_t t = ENC_TRANS[prev][ab];
    if (encoder_cycle_dir == 0) {
        // Idle: a valid gray transition arms a new detent cycle
        if (t == 0) return;
        encoder_cycle_dir = t;
        encoder_cycle_edges = 1;
    } else if (t == 0 || t != encoder_cycle_dir) {
        // Illegal transition or reversal (contact bounce): discard partial cycle
        encoder_cycle_dir = 0;
        encoder_cycle_edges = 0;
    } else {
        encoder_cycle_edges++;
        if (encoder_cycle_edges == 4) {
            // Full quadrature cycle = one physical detent = one step
            encoder_count += encoder_cycle_dir;
            encoder_cycle_dir = 0;
            encoder_cycle_edges = 0;
        }
    }
}

static void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == PIN_ENCODER_BTN) {
        if (events & GPIO_IRQ_EDGE_FALL) {
            button_press_time = time_us_32();
            // Request click
            beep_req_freq = 2000;
            beep_req_dur = 150; // 2ms
        } else if (events & GPIO_IRQ_EDGE_RISE) {
            uint32_t press_duration = time_us_32() - button_press_time;
            if (press_duration > 1000000) {  // 1 second for long press
                button_long_pressed = true;
                beep_req_freq = 1000; 
                beep_req_dur = 150; // 50ms
            } else {
                button_pressed = true; // Short press
                // Note: could add release beep here 
            }
        }
    } else if (gpio == PIN_ENCODER_CLK || gpio == PIN_ENCODER_DATA) {
        encoder_process();
    }
}
