#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "interface.h"
#include "menu.h"
#include "cli.h"
#include "signal_gen.h"
#include "terminal_control.h"

int main(void) {
    // Initialize GPIO FIRST before anything else
    gpio_init(5); // LED pin
    gpio_set_dir(5, GPIO_OUT);
    
    // Blink LED to show we're alive
    for (int i = 0; i < 5; i++) {
        gpio_put(5, 1);
        sleep_ms(100);
        gpio_put(5, 0);
        sleep_ms(100);
    }
    
    // Now init stdio
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);
    sleep_ms(500);

    printf("\n\n=== ECG Monitor Starting ===\n");
    fflush(stdout);
    
    printf("Initializing interface...\n");
    fflush(stdout);
    interface_init();
    interface_splash_animation();
    interface_show_info_screen();
    sleep_ms(100);
    
    printf("Initializing signal generator...\n");
    fflush(stdout);
    signal_gen_init();
    signal_gen_start();
    sleep_ms(100);
    
    printf("Interface initialized\n");
    fflush(stdout);
    sleep_ms(100);
    
    printf("Initializing menu...\n");
    fflush(stdout);
    
    // Blink LED to indicate menu init starting
    gpio_put(5, 1);
    sleep_ms(50);
    gpio_put(5, 0);
    
    menu_init();
    terminal_control_init();
    
    // Another blink to indicate menu init done
    gpio_put(5, 1);
    sleep_ms(50);
    gpio_put(5, 0);
    
    sleep_ms(100);

    printf("Launching signal generator on core 1...\n");
    fflush(stdout);
    multicore_launch_core1(signal_gen_core1_entry);
    sleep_ms(100);

    printf("Menu initialized - Starting main loop\n");
    fflush(stdout);
    
    // Main loop
    absolute_time_t next_blink = get_absolute_time();
    while (true) {
        // Heartbeat - Fast blink to show life
        /*if (absolute_time_diff_us(get_absolute_time(), next_blink) < 0) {
            interface_led_toggle();
            next_blink = make_timeout_time_ms(200);
        }*/

        // Process background tasks (buzzer, terminal blinking, encoder input, etc)
        interface_update();
        terminal_control_update();
        menu_process_input();

        if (!cli_is_streaming()) {
            menu_draw();
            sleep_ms(10);
        } else {
            static absolute_time_t last_draw = 0;
            if (absolute_time_diff_us(last_draw, get_absolute_time()) > 200000) { // 5 Hz refresh during streaming
                menu_draw();
                last_draw = get_absolute_time();
            }
            sleep_us(200);
        }
    }
}
