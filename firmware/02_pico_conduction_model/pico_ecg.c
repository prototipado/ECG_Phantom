#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "interface.h"
#include "menu.h"
#include "signal_gen.h"
#include "terminal_control.h"

int main(void) {
    // Initialize GPIO FIRST before anything else
    //gpio_init(23); // Force the Pico 2 SMPS into PWM mode for lower 3V3 ripple
    //gpio_put(23, 1);
    //gpio_set_dir(23, GPIO_OUT);

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
    signal_gen_set_amplitude(settings.amplitude * 240);
    signal_gen_set_offset(settings.offset);
    signal_gen_calibrate_startup();
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

        // Process background tasks (buzzer, terminal blinking, etc)
        interface_update();
        terminal_control_update();

        // Process input
        menu_process_input();
        
        // Draw Menu
        menu_draw();
        
        // Small delay
        sleep_ms(10);
    }
}
