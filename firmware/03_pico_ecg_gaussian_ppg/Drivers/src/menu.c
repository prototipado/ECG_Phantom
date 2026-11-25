#include "menu.h"
#include "gem_u8g2.h"
#include "pico/stdlib.h"
#include "interface.h"
#include "signal_gen.h"
#include "gaussian_ecg_ppg_model.h"
#include <string.h>
#include <stdio.h>

#define MAX_RHYTHM_ITEMS 8

// ==================== Global Variables ====================
ecg_settings_t settings = {
    .heart_rate = 72,
    .amplitude = 100,
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
static gem_page_t page_conduction;
static gem_page_t page_gaussian;
static gem_page_t page_ppg;
static gem_page_t page_waveforms;
static gem_page_t page_interface;
static gem_page_t page_system;

// ==================== GEM Items ====================
static gem_item_t item_main_signal, item_main_rhythm, item_main_conduction,
                   item_main_gaussian, item_main_ppg, item_main_waveforms,
                   item_main_interface, item_main_system;

static gem_item_t item_signal_hr, item_signal_amplitude, item_signal_offset, item_signal_back;

static gem_item_t item_rhythm[MAX_RHYTHM_ITEMS];
static gem_item_t item_rhythm_back;

// Conduction / event-schedule params
static gem_item_t item_c_pr, item_c_princ, item_c_wenck, item_c_escape,
                   item_c_respdepth, item_c_respfreq, item_c_qtexp, item_c_back;

// Gaussian ECG waveform params
static gem_item_t item_g_pamp, item_g_pmu, item_g_psigma,
                   item_g_qamp, item_g_ramp, item_g_samp,
                   item_g_tamp, item_g_tmu, item_g_tsigma, item_g_back;

// PPG coupling params
static gem_item_t item_ppg_pep, item_ppg_len, item_ppg_pwv,
                   item_ppg_ectamp, item_ppg_ectwiden, item_ppg_ectdelay, item_ppg_back;

static gem_item_t item_wave_sine, item_wave_triangle, item_wave_square, item_wave_pulse, item_wave_back;

static gem_item_t item_interface_buzzer, item_interface_led, item_interface_back;
static gem_item_t item_system_model;
static gem_item_t item_system_paper;
static gem_item_t item_system_version;
static gem_item_t item_system_back;

static void rebuild_rhythm_page(void);

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
    for (uint8_t i = 0; i < MAX_RHYTHM_ITEMS; i++) {
        if (data.pMenuItem == &item_rhythm[i]) {
            gaussian_model_apply_rhythm(i);
            settings.rhythm_index = i;
            settings.heart_rate = (uint16_t)gaussian_model_params.atrial_rate_bpm;
            signal_gen_set_bpm(settings.heart_rate);
            interface_buzzer_beep(1500, 100);
            printf("\n[menu] Rhythm: %s\n", gaussian_model_get_rhythm_name(i));
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
void callback_back_to_model(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_ECG);
    signal_gen_set_amplitude(settings.amplitude * 240);
    interface_buzzer_beep(1500, 100);
}

static void rebuild_rhythm_page(void) {
    uint8_t count = gaussian_model_get_rhythm_count();
    for (uint8_t i = 0; i < MAX_RHYTHM_ITEMS; i++) {
        if (i < count) {
            gem_item_init_button(&item_rhythm[i], (char*)gaussian_model_get_rhythm_name(i), callback_rhythm_selected);
            gem_item_hide(&item_rhythm[i], false);
        } else {
            gem_item_hide(&item_rhythm[i], true);
        }
    }
}

// ==================== GEM Millis ====================
uint32_t gem_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

// ==================== Initialize Menu ====================
void menu_init(void) {
    gem_hal_t hal = gem_hal_u8g2_init(&u8g2);
    hal.millis = gem_millis;
    hal.update_screen = NULL;

    gem_page_init(&page_main, "Main Menu", NULL);
    gem_item_init_label(&item_main_signal, "> Signal");
    gem_item_init_label(&item_main_rhythm, "> Rhythm");
    gem_item_init_label(&item_main_conduction, "> Conduction");
    gem_item_init_label(&item_main_gaussian, "> ECG Waveform");
    gem_item_init_label(&item_main_ppg, "> PPG Coupling");
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

    // Rhythm page (AV conduction presets)
    gem_page_init(&page_rhythm, "Rhythm", &page_main);
    for (uint8_t i = 0; i < MAX_RHYTHM_ITEMS; i++) {
        gem_item_init_button(&item_rhythm[i], "-", callback_rhythm_selected);
        gem_page_add_item(&page_rhythm, &item_rhythm[i]);
    }
    gem_item_init_button(&item_rhythm_back, "Back", callback_back);
    gem_page_add_item(&page_rhythm, &item_rhythm_back);

    // Conduction / event schedule params
    gem_page_init(&page_conduction, "Conduction", &page_main);
    gem_item_init_val_float(&item_c_pr, "PR interval ms", &gaussian_model_params.pr_interval_ms, NULL);
    gem_item_init_val_float(&item_c_princ, "Mobitz PR+ ms", &gaussian_model_params.pr_increment_ms, NULL);
    gem_item_init_val_float(&item_c_escape, "Escape rate bpm", &gaussian_model_params.escape_rate_bpm, NULL);
    gem_item_init_val_float(&item_c_respdepth, "RSA depth", &gaussian_model_params.resp_mod_depth, NULL);
    gem_item_init_val_float(&item_c_respfreq, "RSA freq Hz", &gaussian_model_params.resp_freq_hz, NULL);
    gem_item_init_val_float(&item_c_qtexp, "QT rate exp.", &gaussian_model_params.qt_rate_exponent, NULL);
    gem_item_init_button(&item_c_back, "Back", callback_back);
    gem_page_add_item(&page_conduction, &item_c_pr);
    gem_page_add_item(&page_conduction, &item_c_princ);
    gem_page_add_item(&page_conduction, &item_c_escape);
    gem_page_add_item(&page_conduction, &item_c_respdepth);
    gem_page_add_item(&page_conduction, &item_c_respfreq);
    gem_page_add_item(&page_conduction, &item_c_qtexp);
    gem_page_add_item(&page_conduction, &item_c_back);

    // Gaussian ECG waveform params
    gem_page_init(&page_gaussian, "ECG Waveform", &page_main);
    gem_item_init_val_float(&item_g_pamp, "P amplitude", &gaussian_model_params.p_amp, NULL);
    gem_item_init_val_float(&item_g_pmu, "P offset ms", &gaussian_model_params.p_mu_ms, NULL);
    gem_item_init_val_float(&item_g_psigma, "P width ms", &gaussian_model_params.p_sigma_ms, NULL);
    gem_item_init_val_float(&item_g_qamp, "Q amplitude", &gaussian_model_params.q_amp, NULL);
    gem_item_init_val_float(&item_g_ramp, "R amplitude", &gaussian_model_params.r_amp, NULL);
    gem_item_init_val_float(&item_g_samp, "S amplitude", &gaussian_model_params.s_amp, NULL);
    gem_item_init_val_float(&item_g_tamp, "T amplitude", &gaussian_model_params.t_amp, NULL);
    gem_item_init_val_float(&item_g_tmu, "T offset ms", &gaussian_model_params.t_mu_ms, NULL);
    gem_item_init_val_float(&item_g_tsigma, "T width ms", &gaussian_model_params.t_sigma_ms, NULL);
    gem_item_init_button(&item_g_back, "Back", callback_back);
    gem_page_add_item(&page_gaussian, &item_g_pamp);
    gem_page_add_item(&page_gaussian, &item_g_pmu);
    gem_page_add_item(&page_gaussian, &item_g_psigma);
    gem_page_add_item(&page_gaussian, &item_g_qamp);
    gem_page_add_item(&page_gaussian, &item_g_ramp);
    gem_page_add_item(&page_gaussian, &item_g_samp);
    gem_page_add_item(&page_gaussian, &item_g_tamp);
    gem_page_add_item(&page_gaussian, &item_g_tmu);
    gem_page_add_item(&page_gaussian, &item_g_tsigma);
    gem_page_add_item(&page_gaussian, &item_g_back);

    // PPG coupling params
    gem_page_init(&page_ppg, "PPG Coupling", &page_main);
    gem_item_init_val_float(&item_ppg_pep, "PEP ms", &gaussian_model_params.pep_ms, NULL);
    gem_item_init_val_float(&item_ppg_len, "Path length m", &gaussian_model_params.path_length_m, NULL);
    gem_item_init_val_float(&item_ppg_pwv, "PWV m/s", &gaussian_model_params.pwv_mps, NULL);
    gem_item_init_val_float(&item_ppg_ectamp, "Ectopic amp x", &gaussian_model_params.ectopic_ppg_amp_scale, NULL);
    gem_item_init_val_float(&item_ppg_ectwiden, "Ectopic QRS x", &gaussian_model_params.ectopic_qrs_widen, NULL);
    gem_item_init_val_float(&item_ppg_ectdelay, "Ectopic delay ms", &gaussian_model_params.ectopic_qrs_delay_ms, NULL);
    gem_item_init_button(&item_ppg_back, "Back", callback_back);
    gem_page_add_item(&page_ppg, &item_ppg_pep);
    gem_page_add_item(&page_ppg, &item_ppg_len);
    gem_page_add_item(&page_ppg, &item_ppg_pwv);
    gem_page_add_item(&page_ppg, &item_ppg_ectamp);
    gem_page_add_item(&page_ppg, &item_ppg_ectwiden);
    gem_page_add_item(&page_ppg, &item_ppg_ectdelay);
    gem_page_add_item(&page_ppg, &item_ppg_back);

    // Test Waveforms page
    gem_page_init(&page_waveforms, "Test Waveforms", &page_main);
    gem_item_init_button(&item_wave_sine, "Sine Wave", callback_wave_sine);
    gem_item_init_button(&item_wave_triangle, "Triangle", callback_wave_triangle);
    gem_item_init_button(&item_wave_square, "Square Wave", callback_wave_square);
    gem_item_init_button(&item_wave_pulse, "Pulse (5%)", callback_wave_pulse);
    gem_item_init_button(&item_wave_back, "Back to Model", callback_back_to_model);
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
    gem_item_init_label(&item_system_model, "Model: ECG+PPG Gauss.");
    gem_item_init_label(&item_system_paper, "Paper: Huynh 2026");
    gem_item_init_label(&item_system_version, "Firmware: v1.1-2026");
    gem_item_init_button(&item_system_back, "Back", callback_back);
    gem_page_add_item(&page_system, &item_system_model);
    gem_page_add_item(&page_system, &item_system_paper);
    gem_page_add_item(&page_system, &item_system_version);
    gem_page_add_item(&page_system, &item_system_back);

    // Links
    gem_item_set_linked_page(&item_main_signal, &page_signal);
    gem_item_set_linked_page(&item_main_rhythm, &page_rhythm);
    gem_item_set_linked_page(&item_main_conduction, &page_conduction);
    gem_item_set_linked_page(&item_main_gaussian, &page_gaussian);
    gem_item_set_linked_page(&item_main_ppg, &page_ppg);
    gem_item_set_linked_page(&item_main_waveforms, &page_waveforms);
    gem_item_set_linked_page(&item_main_interface, &page_interface);
    gem_item_set_linked_page(&item_main_system, &page_system);

    gem_page_add_item(&page_main, &item_main_signal);
    gem_page_add_item(&page_main, &item_main_rhythm);
    gem_page_add_item(&page_main, &item_main_conduction);
    gem_page_add_item(&page_main, &item_main_gaussian);
    gem_page_add_item(&page_main, &item_main_ppg);
    gem_page_add_item(&page_main, &item_main_waveforms);
    gem_page_add_item(&page_main, &item_main_interface);
    gem_page_add_item(&page_main, &item_main_system);

    gem_init(&menu_gem, hal, &page_main);
    gem_set_appearance(&menu_gem, GEM_POINTER_ROW, 4, 10, 22, 80);

    rebuild_rhythm_page();

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

