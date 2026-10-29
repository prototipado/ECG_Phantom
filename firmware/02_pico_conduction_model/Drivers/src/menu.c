#include "menu.h"
#include "gem_u8g2.h"
#include "pico/stdlib.h"
#include "interface.h"
#include "signal_gen.h"
#include "cardiac_model.h"
#include <string.h>
#include <stdio.h>

#define MAX_RHYTHM_ITEMS 8

// ==================== Global Variables ====================
ecg_settings_t settings = {
    .heart_rate = 72,
    .amplitude = 100,  // Default: 100%
    .offset = 0,
    .is_playing = false,
    .rhythm_index = 0,
    .buzzer_enabled = true,
    .led_enabled = true,
};

gem_t menu_gem;

// ==================== GEM Pages ====================
static gem_page_t page_main;
static gem_page_t page_signal;
static gem_page_t page_rhythm;
static gem_page_t page_model;
static gem_page_t page_waveforms;
static gem_page_t page_interface;
static gem_page_t page_system;

// ==================== GEM Items ====================
// Main page
static gem_item_t item_main_signal;
static gem_item_t item_main_rhythm;
static gem_item_t item_main_model;
static gem_item_t item_main_waveforms;
static gem_item_t item_main_interface;
static gem_item_t item_main_system;

// Signal page
static gem_item_t item_signal_hr;
static gem_item_t item_signal_amplitude;
static gem_item_t item_signal_offset;
static gem_item_t item_signal_back;

// Rhythm page (dynamic list of presets)
static gem_item_t item_rhythm[MAX_RHYTHM_ITEMS];
static gem_item_t item_rhythm_back;

// Model page (editable ODE parameters)
static gem_item_t item_model_hrv_enabled;
static gem_item_t item_model_hrv_depth;
static gem_item_t item_model_baseline_enabled;
static gem_item_t item_model_baseline_amp;
static gem_item_t item_model_baseline_freq;
static gem_item_t item_model_alpha1;
static gem_item_t item_model_alpha2;
static gem_item_t item_model_alpha3;
static gem_item_t item_model_alpha4;
static gem_item_t item_model_delay;
static gem_item_t item_model_back;

// Test Waveforms page
static gem_item_t item_wave_sine;
static gem_item_t item_wave_triangle;
static gem_item_t item_wave_square;
static gem_item_t item_wave_pulse;
static gem_item_t item_wave_back;

// Interface page
static gem_item_t item_interface_buzzer;
static gem_item_t item_interface_led;
static gem_item_t item_interface_back;

// System page
static gem_item_t item_system_model;
static gem_item_t item_system_paper;
static gem_item_t item_system_version;
static gem_item_t item_system_back;

// ==================== Callbacks ====================
void callback_heart_rate_changed(gem_callback_data_t data) {
    if (settings.heart_rate < 15) settings.heart_rate = 15;
    if (settings.heart_rate > 250) settings.heart_rate = 250;
    signal_gen_set_bpm(settings.heart_rate);
}

void callback_amplitude_changed(gem_callback_data_t data) {
    if (settings.amplitude > 100) settings.amplitude = 100;
    signal_gen_set_amplitude(settings.amplitude * 240);
}

void callback_offset_changed(gem_callback_data_t data) {
    if (settings.offset < -16384) settings.offset = -16384;
    if (settings.offset > 16384) settings.offset = 16384;
    signal_gen_set_offset(settings.offset);
}

void callback_back(gem_callback_data_t data) {
    gem_page_t* current = gem_get_current_page(&menu_gem);
    if (current != NULL) {
        gem_page_t* parent = gem_page_get_parent(current);
        if (parent != NULL) {
            gem_set_current_page(&menu_gem, parent);
        }
    }
}

void callback_rhythm_selected(gem_callback_data_t data) {
    uint8_t count = cardiac_model_get_rhythm_count();
    for (uint8_t i = 0; i < count && i < MAX_RHYTHM_ITEMS; i++) {
        if (data.pMenuItem == &item_rhythm[i]) {
            cardiac_model_apply_rhythm(i);
            settings.rhythm_index = i;
            // Convenience heart-rate presets for rate-dependent rhythms
            if (i == CARDIAC_RHYTHM_SINUS_BRADYCARDIA) {
                settings.heart_rate = 45;
                signal_gen_set_bpm(settings.heart_rate);
            } else if (i == CARDIAC_RHYTHM_SINUS_TACHYCARDIA) {
                settings.heart_rate = 130;
                signal_gen_set_bpm(settings.heart_rate);
            }
            interface_buzzer_beep(1500, 100);
            printf("\n[menu] Rhythm: %s\n", cardiac_model_get_rhythm_name(i));
            fflush(stdout);
            break;
        }
    }
}

void callback_wave_sine(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_SINE);
    signal_gen_set_bpm(settings.heart_rate);
    signal_gen_set_amplitude(settings.amplitude * 327);
    interface_buzzer_beep(1500, 100);
}
void callback_wave_triangle(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_TRIANGLE);
    signal_gen_set_bpm(settings.heart_rate);
    signal_gen_set_amplitude(settings.amplitude * 327);
    interface_buzzer_beep(1500, 100);
}
void callback_wave_square(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_SQUARE);
    signal_gen_set_bpm(settings.heart_rate);
    signal_gen_set_amplitude(settings.amplitude * 327);
    interface_buzzer_beep(1500, 100);
}
void callback_wave_pulse(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_PULSE);
    signal_gen_set_bpm(settings.heart_rate);
    signal_gen_set_amplitude(settings.amplitude * 327);
    interface_buzzer_beep(1500, 100);
}
void callback_back_to_cardiac(gem_callback_data_t data) {
    // No-op helper kept for symmetry; cardiac model is always active unless
    // a test waveform was explicitly selected.
    signal_gen_set_signal_type(SIGNAL_TYPE_ECG);
    signal_gen_set_amplitude(settings.amplitude * 240);
    interface_buzzer_beep(1500, 100);
}

// ==================== GEM Millis ====================
uint32_t gem_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

// ==================== Initialize Menu ====================
void menu_init(void) {
    printf("  [menu] Setting up HAL...\n");
    fflush(stdout);

    gem_hal_t hal = gem_hal_u8g2_init(&u8g2);
    hal.millis = gem_millis;
    hal.update_screen = NULL;

    gem_page_init(&page_main, "Main Menu", NULL);

    gem_item_init_label(&item_main_signal, "> Signal");
    gem_item_init_label(&item_main_rhythm, "> Rhythm");
    gem_item_init_label(&item_main_model, "> Model");
    gem_item_init_label(&item_main_waveforms, "> Test Waveforms");
    gem_item_init_label(&item_main_interface, "> Interface");
    gem_item_init_label(&item_main_system, "> System");

    // Signal page
    gem_page_init(&page_signal, "Signal", &page_main);
    gem_item_init_val_uint16(&item_signal_hr, "Heart Rate", &settings.heart_rate, callback_heart_rate_changed);
    gem_item_init_val_uint16(&item_signal_amplitude, "Amplitude", &settings.amplitude, callback_amplitude_changed);
    gem_item_init_val_int16(&item_signal_offset, "Offset", &settings.offset, callback_offset_changed);
    gem_item_init_button(&item_signal_back, "Back", callback_back);
    gem_page_add_item(&page_signal, &item_signal_hr);
    gem_page_add_item(&page_signal, &item_signal_amplitude);
    gem_page_add_item(&page_signal, &item_signal_offset);
    gem_page_add_item(&page_signal, &item_signal_back);

    // Rhythm page (dynamic from cardiac_model presets)
    gem_page_init(&page_rhythm, "Rhythm", &page_main);
    uint8_t rhythm_count = cardiac_model_get_rhythm_count();
    for (uint8_t i = 0; i < rhythm_count && i < MAX_RHYTHM_ITEMS; i++) {
        gem_item_init_button(&item_rhythm[i], (char*)cardiac_model_get_rhythm_name(i), callback_rhythm_selected);
        gem_page_add_item(&page_rhythm, &item_rhythm[i]);
    }
    gem_item_init_button(&item_rhythm_back, "Back", callback_back);
    gem_page_add_item(&page_rhythm, &item_rhythm_back);

    // Model page (editable ODE parameters, see cardiac_model.h)
    gem_page_init(&page_model, "Model Params", &page_main);
    gem_item_init_val_bool(&item_model_hrv_enabled, "HRV Enabled", &cardiac_model_params.hrv_enabled, NULL);
    gem_item_init_val_float(&item_model_hrv_depth, "HRV Depth", &cardiac_model_params.hrv_depth, NULL);
    gem_item_init_val_bool(&item_model_baseline_enabled, "Base.Wander En.", &cardiac_model_params.baseline_wander_enabled, NULL);
    gem_item_init_val_float(&item_model_baseline_amp, "Baseline mV", &cardiac_model_params.baseline_z0_mV, NULL);
    gem_item_init_val_float(&item_model_baseline_freq, "Baseline Hz", &cardiac_model_params.baseline_f0_hz, NULL);
    gem_item_init_val_float(&item_model_alpha1, "Alpha P-wave", &cardiac_model_params.alpha1, NULL);
    gem_item_init_val_float(&item_model_alpha2, "Alpha Ta-wave", &cardiac_model_params.alpha2, NULL);
    gem_item_init_val_float(&item_model_alpha3, "Alpha QRS", &cardiac_model_params.alpha3, NULL);
    gem_item_init_val_float(&item_model_alpha4, "Alpha T-wave", &cardiac_model_params.alpha4, NULL);
    gem_item_init_val_float(&item_model_delay, "Node Delay ms", &cardiac_model_params.tau_node_ms, NULL);
    gem_item_init_button(&item_model_back, "Back", callback_back);
    gem_page_add_item(&page_model, &item_model_hrv_enabled);
    gem_page_add_item(&page_model, &item_model_hrv_depth);
    gem_page_add_item(&page_model, &item_model_baseline_enabled);
    gem_page_add_item(&page_model, &item_model_baseline_amp);
    gem_page_add_item(&page_model, &item_model_baseline_freq);
    gem_page_add_item(&page_model, &item_model_alpha1);
    gem_page_add_item(&page_model, &item_model_alpha2);
    gem_page_add_item(&page_model, &item_model_alpha3);
    gem_page_add_item(&page_model, &item_model_alpha4);
    gem_page_add_item(&page_model, &item_model_delay);
    gem_page_add_item(&page_model, &item_model_back);

    // Test Waveforms page (hardware calibration signals)
    gem_page_init(&page_waveforms, "Test Waveforms", &page_main);
    gem_item_init_button(&item_wave_sine, "Sine Wave", callback_wave_sine);
    gem_item_init_button(&item_wave_triangle, "Triangle", callback_wave_triangle);
    gem_item_init_button(&item_wave_square, "Square Wave", callback_wave_square);
    gem_item_init_button(&item_wave_pulse, "Pulse (5%)", callback_wave_pulse);
    gem_item_init_button(&item_wave_back, "Back to Cardiac", callback_back_to_cardiac);
    gem_page_add_item(&page_waveforms, &item_wave_sine);
    gem_page_add_item(&page_waveforms, &item_wave_triangle);
    gem_page_add_item(&page_waveforms, &item_wave_square);
    gem_page_add_item(&page_waveforms, &item_wave_pulse);
    gem_page_add_item(&page_waveforms, &item_wave_back);

    // Interface page
    gem_page_init(&page_interface, "Interface", &page_main);
    gem_item_init_val_bool(&item_interface_buzzer, "Buzzer", &settings.buzzer_enabled, NULL);
    gem_item_init_val_bool(&item_interface_led, "Led", &settings.led_enabled, NULL);
    gem_item_init_button(&item_interface_back, "Back", callback_back);
    gem_page_add_item(&page_interface, &item_interface_buzzer);
    gem_page_add_item(&page_interface, &item_interface_led);
    gem_page_add_item(&page_interface, &item_interface_back);

    // System page
    gem_page_init(&page_system, "System", &page_main);
    gem_item_init_label(&item_system_model, "Model: VdP + FHN");
    gem_item_init_label(&item_system_paper, "Paper: Quiroz-J 2018");
    gem_item_init_label(&item_system_version, "Firmware: v1.1-2026");
    gem_item_init_button(&item_system_back, "Back", callback_back);
    gem_page_add_item(&page_system, &item_system_model);
    gem_page_add_item(&page_system, &item_system_paper);
    gem_page_add_item(&page_system, &item_system_version);
    gem_page_add_item(&page_system, &item_system_back);

    // Setup links
    gem_item_set_linked_page(&item_main_signal, &page_signal);
    gem_item_set_linked_page(&item_main_rhythm, &page_rhythm);
    gem_item_set_linked_page(&item_main_model, &page_model);
    gem_item_set_linked_page(&item_main_waveforms, &page_waveforms);
    gem_item_set_linked_page(&item_main_interface, &page_interface);
    gem_item_set_linked_page(&item_main_system, &page_system);

    gem_page_add_item(&page_main, &item_main_signal);
    gem_page_add_item(&page_main, &item_main_rhythm);
    gem_page_add_item(&page_main, &item_main_model);
    gem_page_add_item(&page_main, &item_main_waveforms);
    gem_page_add_item(&page_main, &item_main_interface);
    gem_page_add_item(&page_main, &item_main_system);

    gem_init(&menu_gem, hal, &page_main);
    gem_set_appearance(&menu_gem, GEM_POINTER_ROW, 4, 10, 22, 80);

    printf("  [menu] Menu initialization complete!\n");
    fflush(stdout);
}

// ==================== Process Menu Input ====================
void menu_process_input(void) {
    int32_t delta = interface_get_encoder_delta();

    if (delta != 0) {
        gem_key_t key = delta > 0 ? GEM_KEY_DOWN : GEM_KEY_UP;
        bool can_move = true;
        // During value editing, let GEM apply the step as-is (limits handled there)
        if (!menu_gem.is_editing && menu_gem.current_page != NULL &&
            menu_gem.current_page->item_count > 0) {
            int current_idx = menu_gem.scroll_offset + menu_gem.cursor_row;
            if (delta > 0) {
                can_move = (current_idx < menu_gem.current_page->item_count - 1);
            } else {
                can_move = (current_idx > 0);
            }
        }
        if (can_move) {
            gem_register_key(&menu_gem, key);
        }
        interface_buzzer_beep_nonblocking(can_move ? 4000 : 1200, can_move ? 50 : 80);
    }

    if (interface_get_button_pressed()) {
        gem_register_key(&menu_gem, GEM_KEY_OK);
    }

    if (interface_get_button_long_pressed()) {
        gem_register_key(&menu_gem, GEM_KEY_CANCEL);
    }
}

// ==================== Draw Menu ====================
void menu_draw(void) {
    gem_draw(&menu_gem);

    u8g2_SetDrawColor(&u8g2, 1);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);

    float vbatt = interface_get_battery_voltage();
    bool usb = interface_is_usb_connected();
    char sys_info[64];
    snprintf(sys_info, sizeof(sys_info), "%.1fV%s", vbatt, usb ? "[USB]" : "");
    u8g2_DrawStr(&u8g2, 0, 9, sys_info);

    int speaker_x = u8g2_GetStrWidth(&u8g2, sys_info) + 5;
    u8g2_SetFont(&u8g2, u8g2_font_open_iconic_play_1x_t);
    u8g2_DrawGlyph(&u8g2, speaker_x, 9, settings.buzzer_enabled ? 79 : 81);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);

    uint16_t bpm = signal_gen_get_bpm();
    snprintf(sys_info, sizeof(sys_info), "%dBPM", bpm);
    u8g2_DrawStr(&u8g2, speaker_x + 14, 9, sys_info);

    signal_type_t type = signal_gen_get_signal_type();
    const char* type_str = "MODEL";
    switch (type) {
        case SIGNAL_TYPE_ECG:      type_str = "MODEL"; break;
        case SIGNAL_TYPE_SINE:     type_str = "SINE"; break;
        case SIGNAL_TYPE_TRIANGLE: type_str = "TRIA"; break;
        case SIGNAL_TYPE_SQUARE:   type_str = "SQUA"; break;
        case SIGNAL_TYPE_PULSE:    type_str = "PULS"; break;
    }
    snprintf(sys_info, sizeof(sys_info), "%s", type_str);
    u8g2_DrawStr(&u8g2, 128 - u8g2_GetStrWidth(&u8g2, sys_info), 9, sys_info);

    u8g2_DrawHLine(&u8g2, 0, 11, 128);
    u8g2_SendBuffer(&u8g2);
}
