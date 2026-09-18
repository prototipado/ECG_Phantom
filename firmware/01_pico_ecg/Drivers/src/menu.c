#include "menu.h"
#include "gem_u8g2.h"
#include "pico/stdlib.h"
#include "interface.h"
#include "signal_gen.h"
#include "ecg_signals.h"
#include "signal_registry.h"
#include <string.h>
#include <stdio.h>

// ==================== Global Variables ====================
ecg_settings_t settings = {
    .ecg_type = 0,
    .pathology = 0,
    .heart_rate = 72,
    .amplitude = 100,  // Default: 100%
    .noise_level = 0,
    .is_playing = false,
    .loop_enabled = false,
//   .gain = 100,
    .offset = 0,
    .lead = 0,
    .led_mode = 0,
    .buzzer_enabled = true,
    .led_enabled = true,
    .encoder_mode = 0,
    .selected_file = 0,
    .show_diagnostics = false,
};

gem_t menu_gem;

// ==================== File Info Pagination ====================
static uint8_t file_info_page = 0;  // Current page of file info display
static uint8_t file_info_max_pages = 1;  // Total pages available

// ==================== GEM Pages ====================
static gem_page_t page_main;
static gem_page_t page_signal;
static gem_page_t page_signal_type;
//static gem_page_t page_playback;
//static gem_page_t page_output;
static gem_page_t page_interface;
static gem_page_t page_files;
static gem_page_t page_file_info;
static gem_page_t page_system;

// ==================== GEM Items ====================
// Main page
static gem_item_t item_main_signal;
//static gem_item_t item_main_playback;
//static gem_item_t item_main_output;
static gem_item_t item_main_interface;
static gem_item_t item_main_files;
static gem_item_t item_main_system;

// Signal page
static gem_item_t item_signal_type;
static gem_item_t item_signal_hr;
static gem_item_t item_signal_amplitude;
static gem_item_t item_signal_offset;
static gem_item_t item_signal_back;

// Signal Type page
static gem_item_t item_signal_type_cardiac;
static gem_item_t item_signal_type_sine;
static gem_item_t item_signal_type_triangle;
static gem_item_t item_signal_type_square;
static gem_item_t item_signal_type_pulse;
static gem_item_t item_signal_type_frequency;
static gem_item_t item_signal_type_back;

// Playback page
//static gem_item_t item_playback_play;
//static gem_item_t item_playback_loop;
//static gem_item_t item_playback_back;

// Output page
//static gem_item_t item_output_gain;
//static gem_item_t item_output_offset;
//static gem_item_t item_output_back;

// Interface page
static gem_item_t item_interface_buzzer;
static gem_item_t item_interface_led;

static gem_item_t item_interface_back;

// Files page
static gem_item_t item_files_back;
static gem_item_t item_files[MAX_SIGNALS];  // Dynamic array for signal files
static uint8_t num_file_items = 0;

// File Info page (details display)
static gem_item_t item_file_info_back;

// System page
static gem_item_t item_system_model;
static gem_item_t item_system_version;
static gem_item_t item_system_back;

// ==================== Callbacks ====================
void callback_heart_rate_changed(gem_callback_data_t data) {
    if (settings.heart_rate < 1) settings.heart_rate = 1;
    if (settings.heart_rate > 3000) settings.heart_rate = 3000;
    
    // Update signal generator in real-time
    signal_gen_set_bpm(settings.heart_rate);
}

void callback_amplitude_changed(gem_callback_data_t data) {
    if (settings.amplitude > 100) settings.amplitude = 100;
    // Update signal amplitude in real-time
    signal_type_t type = signal_gen_get_signal_type();
    if (type == SIGNAL_TYPE_ECG) {
        signal_gen_set_amplitude(settings.amplitude * 240); // 100% -> 24000
    } else {
        signal_gen_set_amplitude(settings.amplitude * 327); // 100% -> 32700
    }
}

void callback_offset_changed(gem_callback_data_t data) {
    if (settings.offset < -16384) settings.offset = -16384;
    if (settings.offset > 16384) settings.offset = 16384;
    signal_gen_set_offset(settings.offset);
}

void callback_play_pause(gem_callback_data_t data) {
    settings.is_playing = !settings.is_playing;
    interface_buzzer_beep(settings.is_playing ? 1500 : 1000, 100);
}

void callback_gain_changed(gem_callback_data_t data) {
    if (settings.gain < 10) settings.gain = 10;
    if (settings.gain > 1000) settings.gain = 1000;
}

void callback_back(gem_callback_data_t data) {
    // Navigate back to parent page
    gem_page_t* current = gem_get_current_page(&menu_gem);
    if (current != NULL) {
        gem_page_t* parent = gem_page_get_parent(current);
        if (parent != NULL) {
            gem_set_current_page(&menu_gem, parent);
        }
    }
}

// Signal Type callbacks
void callback_signal_type_cardiac(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_ECG);
    // Load first available ECG signal from registry
    const ecg_struct_t* signal = signal_registry_get_by_index(0);
    if (signal != NULL) {
        signal_gen_set_signal(signal);
    }
    signal_gen_set_amplitude(settings.amplitude * 240);  // Apply amplitude to ECG
    signal_gen_set_offset(settings.offset * 16);  // Apply offset to ECG
    interface_buzzer_beep(1500, 100);
}

void callback_signal_type_sine(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_SINE);
    signal_gen_set_bpm(settings.heart_rate);
    signal_gen_set_amplitude(settings.amplitude * 327);
    signal_gen_set_offset(settings.offset * 16);
    interface_buzzer_beep(1500, 100);
}

void callback_signal_type_triangle(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_TRIANGLE);
    signal_gen_set_bpm(settings.heart_rate);
    signal_gen_set_amplitude(settings.amplitude * 327);
    signal_gen_set_offset(settings.offset * 16);
    interface_buzzer_beep(1500, 100);
}

void callback_signal_type_square(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_SQUARE);
    signal_gen_set_bpm(settings.heart_rate);
    signal_gen_set_amplitude(settings.amplitude * 327);
    signal_gen_set_offset(settings.offset * 16);
    interface_buzzer_beep(1500, 100);
}

void callback_signal_type_pulse(gem_callback_data_t data) {
    signal_gen_set_signal_type(SIGNAL_TYPE_PULSE);
    signal_gen_set_bpm(settings.heart_rate);
    signal_gen_set_amplitude(settings.amplitude * 327);
    signal_gen_set_offset(settings.offset * 16);
    interface_buzzer_beep(1500, 100);
}

// ==================== File Selection Callbacks ====================
void callback_file_selected(gem_callback_data_t data) {
    // Find which file item was selected by comparing with pMenuItem
    uint8_t file_index = 0;
    for (uint8_t i = 0; i < num_file_items; i++) {
        if (data.pMenuItem == &item_files[i]) {
            file_index = i;
            break;
        }
    }
    
    if (file_index >= signal_registry_get_count()) return;
    
    const ecg_struct_t* signal = signal_registry_get_by_index(file_index);
    if (signal == NULL) return;
    
    // Load the selected signal
    signal_gen_set_signal(signal);
    settings.selected_file = file_index;
    interface_buzzer_beep(1500, 100);
    
    printf("\n[menu] Selected signal: %s\n", signal_registry_get_filename(file_index));
    printf("  Rhythms: %s\n", signal->rhythms);
    printf("  Cond. Abnor: %s\n", signal->cond_abnor);
    printf("  Hypertrophies: %s\n", signal->hypertrophies);
    printf("  Ischemia: %s\n", signal->ischemia);
    printf("  Pacing: %s\n", signal->pacing);
    printf("  Extrasystolies: %s\n", signal->extrasystolies);
    printf("  Rows: %d, Cols: %d\n", signal->rows, signal->cols);
    fflush(stdout);
    
    // Navigate to info page
    gem_set_current_page(&menu_gem, &page_file_info);
}

// ==================== GEM Millis ====================
uint32_t gem_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

// ==================== Initialize Menu ====================
void menu_init(void) {
    printf("  [menu] Setting up HAL...\n");
    fflush(stdout);
    
    // 1. Setup HAL
    gem_hal_t hal = gem_hal_u8g2_init(&u8g2);
    hal.millis = gem_millis;
    hal.update_screen = NULL; // Disable auto-update to allow drawing overlays

    printf("  [menu] Initializing main page...\n");
    fflush(stdout);
    
    // 2. Setup Main Page
    gem_page_init(&page_main, "Main Menu", NULL);

    printf("  [menu] Setting up main page items...\n");
    fflush(stdout);
    
    // Main page items - submenus
    gem_item_init_label(&item_main_signal, "> Signal");
    //gem_item_init_label(&item_main_playback, "> Playback");
    //gem_item_init_label(&item_main_output, "> Output");
    gem_item_init_label(&item_main_interface, "> Interface");
    gem_item_init_label(&item_main_files, "> Files");
    gem_item_init_label(&item_main_system, "> System");
    
    printf("  [menu] Initializing signal page...\n");
    fflush(stdout);
    
    // 3. Setup Signal Page
    gem_page_init(&page_signal, "Signal", &page_main);
    gem_item_init_label(&item_signal_type, "> Type");
    gem_item_init_val_uint16(&item_signal_hr, "Heart Rate", &settings.heart_rate, callback_heart_rate_changed);
    gem_item_init_val_uint16(&item_signal_amplitude, "Amplitude", &settings.amplitude, callback_amplitude_changed);
    gem_item_init_val_int16(&item_signal_offset, "Offset", &settings.offset, callback_offset_changed);
    gem_item_init_button(&item_signal_back, "Back", callback_back);
    gem_page_add_item(&page_signal, &item_signal_type);
    gem_page_add_item(&page_signal, &item_signal_hr);
    gem_page_add_item(&page_signal, &item_signal_amplitude);
    gem_page_add_item(&page_signal, &item_signal_offset);
    gem_page_add_item(&page_signal, &item_signal_back);
    
    printf("  [menu] Initializing signal type page...\n");
    fflush(stdout);
    
    // 3b. Setup Signal Type Page
    gem_page_init(&page_signal_type, "Signal Type", &page_signal);
    gem_item_init_button(&item_signal_type_cardiac, "Cardiac (ECG)", callback_signal_type_cardiac);
    gem_item_init_button(&item_signal_type_sine, "Sine Wave", callback_signal_type_sine);
    gem_item_init_button(&item_signal_type_triangle, "Triangle", callback_signal_type_triangle);
    gem_item_init_button(&item_signal_type_square, "Square Wave", callback_signal_type_square);
    gem_item_init_button(&item_signal_type_pulse, "Pulse (5%)", callback_signal_type_pulse);
    gem_item_init_button(&item_signal_type_back, "Back", callback_back);
    gem_page_add_item(&page_signal_type, &item_signal_type_cardiac);
    gem_page_add_item(&page_signal_type, &item_signal_type_sine);
    gem_page_add_item(&page_signal_type, &item_signal_type_triangle);
    gem_page_add_item(&page_signal_type, &item_signal_type_square);
    gem_page_add_item(&page_signal_type, &item_signal_type_pulse);
    gem_page_add_item(&page_signal_type, &item_signal_type_back);
    
    // Link signal type to main signal page
    gem_item_set_linked_page(&item_signal_type, &page_signal_type);

    printf("  [menu] Initializing playback page...\n");
    fflush(stdout);
    
    // 4. Setup Playback Page
    //gem_page_init(&page_playback, "Playback", &page_main);
    //gem_item_init_val_bool(&item_playback_play, "Play/Pause", &settings.is_playing, callback_play_pause);
    //gem_item_init_val_bool(&item_playback_loop, "Loop", &settings.loop_enabled, NULL);
    //gem_item_init_button(&item_playback_back, "Back", callback_back);
    //gem_page_add_item(&page_playback, &item_playback_play);
    //gem_page_add_item(&page_playback, &item_playback_loop);
    //gem_page_add_item(&page_playback, &item_playback_back);

    //printf("  [menu] Initializing output page...\n");
    //fflush(stdout);
    
    // 5. Setup Output Page
    //gem_page_init(&page_output, "Output", &page_main);
    //gem_item_init_val_uint16(&item_output_gain, "Gain", &settings.gain, callback_gain_changed);
    //gem_item_init_val_int16(&item_output_offset, "Offset", &settings.offset, callback_offset_changed);
    //gem_item_init_button(&item_output_back, "Back", callback_back);
    //gem_page_add_item(&page_output, &item_output_gain);
    //gem_page_add_item(&page_output, &item_output_offset);
    //gem_page_add_item(&page_output, &item_output_back);

    //printf("  [menu] Initializing interface page...\n");
    //fflush(stdout);
    
    // 6. Setup Interface Page
    gem_page_init(&page_interface, "Interface", &page_main);
    gem_item_init_val_bool(&item_interface_buzzer, "Buzzer", &settings.buzzer_enabled, NULL);
    gem_item_init_val_bool(&item_interface_led, "Led", &settings.led_enabled, NULL);
    gem_item_init_button(&item_interface_back, "Back", callback_back);
    gem_page_add_item(&page_interface, &item_interface_buzzer);
    gem_page_add_item(&page_interface, &item_interface_led);
    gem_page_add_item(&page_interface, &item_interface_back);

    printf("  [menu] Initializing files page...\n");
    fflush(stdout);
    
    // 7. Setup Files Page - Dynamic
    gem_page_init(&page_files, "Files", &page_main);
    
    // Add all available signals dynamically
    uint8_t num_signals = signal_registry_get_count();
    num_file_items = num_signals;
    
    for (uint8_t i = 0; i < num_signals && i < MAX_SIGNALS; i++) {
        const char* filename = signal_registry_get_filename(i);
        gem_item_init_button(&item_files[i], (char*)filename, callback_file_selected);
        gem_page_add_item(&page_files, &item_files[i]);
    }
    
    gem_item_init_button(&item_files_back, "Back", callback_back);
    gem_page_add_item(&page_files, &item_files_back);

    printf("  [menu] Initializing file info page...\n");
    fflush(stdout);
    
    // 7b. Setup File Info Page (for signal details)
    gem_page_init(&page_file_info, "Signal Info", &page_files);
    gem_item_init_button(&item_file_info_back, "Back", callback_back);
    gem_page_add_item(&page_file_info, &item_file_info_back);

    printf("  [menu] Initializing system page...\n");
    fflush(stdout);
    
    // 8. Setup System Page
    gem_page_init(&page_system, "System", &page_main);
    gem_item_init_label(&item_system_model, "Model: Real ECG 9ch");
    gem_item_init_label(&item_system_version, "Firmware: v1.1-2026");
    gem_item_init_button(&item_system_back, "Back", callback_back);
    gem_page_add_item(&page_system, &item_system_model);
    gem_page_add_item(&page_system, &item_system_version);
    gem_page_add_item(&page_system, &item_system_back);

    printf("  [menu] Setting up subpage links...\n");
    fflush(stdout);
    
    // Setup links
    gem_item_set_linked_page(&item_main_signal, &page_signal);
    //gem_item_set_linked_page(&item_main_playback, &page_playback);
    //gem_item_set_linked_page(&item_main_output, &page_output);
    gem_item_set_linked_page(&item_main_interface, &page_interface);
    gem_item_set_linked_page(&item_main_files, &page_files);
    gem_item_set_linked_page(&item_main_system, &page_system);
    
    // Add to main page
    gem_page_add_item(&page_main, &item_main_signal);
    //gem_page_add_item(&page_main, &item_main_playback);
    //gem_page_add_item(&page_main, &item_main_output);
    gem_page_add_item(&page_main, &item_main_interface);
    gem_page_add_item(&page_main, &item_main_files);
    gem_page_add_item(&page_main, &item_main_system);

    printf("  [menu] Initializing GEM core...\n");
    fflush(stdout);
    
    // 5. Init GEM
    gem_init(&menu_gem, hal, &page_main);
    
    // 5.1 Set Appearance (Shift menu down for system info)
    // top_offset = 22 (System info 0-12, Title 12-22, Items start at 22)
    gem_set_appearance(&menu_gem, GEM_POINTER_ROW, 4, 10, 22, 80);
    
    // 6. Set default signal from registry
    printf("  [menu] Setting default ECG signal...\n");
    fflush(stdout);
    const ecg_struct_t* default_signal = signal_registry_get_by_index(0);
    if (default_signal != NULL) {
        signal_gen_set_signal(default_signal);
    }
    
    printf("  [menu] Menu initialization complete!\n");
    fflush(stdout);
}

// ==================== Process Menu Input ====================
void menu_process_input(void) {
    // Handle encoder rotation
    int32_t delta = interface_get_encoder_delta();
    
    // Special handling for file info page - navigate pages instead of menu items
    if (menu_gem.current_page == &page_file_info && delta != 0) {
        bool can_move = (delta > 0 && file_info_page < file_info_max_pages - 1) ||
                        (delta < 0 && file_info_page > 0);
        if (can_move) {
            if (delta > 0) {
                file_info_page++;  // Next page
            } else {
                file_info_page--;  // Previous page
            }
        }
        interface_buzzer_beep_nonblocking(can_move ? 4000 : 1200, can_move ? 50 : 80);
    } else if (delta != 0) {
        // Normal menu navigation
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
    
    // Handle button press (OK)
    if (interface_get_button_pressed()) {
        gem_register_key(&menu_gem, GEM_KEY_OK);
    }
    
    // Handle button long press (Cancel/Back)
    if (interface_get_button_long_pressed()) {
        gem_register_key(&menu_gem, GEM_KEY_CANCEL);
    }
}

// ==================== Word Wrap Helper ====================
// Wraps text to fit within max_width pixels, filling wrapped_lines array
// Returns number of lines created
static uint8_t word_wrap_text(const char* text, char wrapped_lines[20][64], uint8_t max_lines, uint16_t max_width) {
    if (text == NULL || strlen(text) == 0) {
        wrapped_lines[0][0] = '\0';
        return 1;
    }
    
    uint8_t line_idx = 0;
    uint16_t current_width = 0;
    char current_line[128];
    current_line[0] = '\0';
    
    // Tokenize by spaces
    char text_copy[256];
    strncpy(text_copy, text, sizeof(text_copy) - 1);
    text_copy[sizeof(text_copy) - 1] = '\0';
    
    char* word = strtok(text_copy, " ");
    
    while (word != NULL && line_idx < max_lines) {
        // Calculate width of current_line + space + word
        char test_line[128];
        if (current_line[0] == '\0') {
            snprintf(test_line, sizeof(test_line), "%s", word);
        } else {
            snprintf(test_line, sizeof(test_line), "%s %s", current_line, word);
        }
        
        uint16_t test_width = u8g2_GetStrWidth(&u8g2, test_line);
        
        if (test_width <= max_width) {
            // Word fits on current line
            strncpy(current_line, test_line, sizeof(current_line) - 1);
            current_line[sizeof(current_line) - 1] = '\0';
        } else {
            // Word doesn't fit, save current line and start new one
            if (current_line[0] != '\0') {
                strncpy(wrapped_lines[line_idx], current_line, 63);
                wrapped_lines[line_idx][63] = '\0';
                line_idx++;
            }
            
            // Start new line with this word
            strncpy(current_line, word, sizeof(current_line) - 1);
            current_line[sizeof(current_line) - 1] = '\0';
        }
        
        word = strtok(NULL, " ");
    }
    
    // Save last line if not empty
    if (current_line[0] != '\0' && line_idx < max_lines) {
        strncpy(wrapped_lines[line_idx], current_line, 63);
        wrapped_lines[line_idx][63] = '\0';
        line_idx++;
    }
    
    return line_idx;
}

// ==================== Draw Menu ====================
void menu_draw(void) {
    // If on file info page, draw custom signal info instead of standard menu
    if (menu_gem.current_page == &page_file_info && settings.selected_file < signal_registry_get_count()) {
        const ecg_struct_t* signal = signal_registry_get_by_index(settings.selected_file);
        if (signal != NULL) {
            // Clear display for custom drawing
            u8g2_ClearBuffer(&u8g2);
            
            // Use GEM's original font (6x10)
            u8g2_SetDrawColor(&u8g2, 1);
            u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
            
            // Create raw information strings (will be wrapped)
            const char* info_title = signal_registry_get_filename(settings.selected_file);
            
            char raw_info[8][256];  // Raw info strings before wrapping
            uint8_t raw_count = 0;
            
            // Prepare raw information strings
            snprintf(raw_info[raw_count], sizeof(raw_info[0]), "File: %s", info_title);
            raw_count++;
            
            snprintf(raw_info[raw_count], sizeof(raw_info[0]), "Rhythms: %s", signal->rhythms);
            raw_count++;
            
            snprintf(raw_info[raw_count], sizeof(raw_info[0]), "Cond.Abnor: %s", signal->cond_abnor);
            raw_count++;
            
            snprintf(raw_info[raw_count], sizeof(raw_info[0]), "Hypertrop: %s", signal->hypertrophies);
            raw_count++;
            
            snprintf(raw_info[raw_count], sizeof(raw_info[0]), "Ischemia: %s", signal->ischemia);
            raw_count++;
            
            snprintf(raw_info[raw_count], sizeof(raw_info[0]), "Pacing: %s", signal->pacing);
            raw_count++;
            
            snprintf(raw_info[raw_count], sizeof(raw_info[0]), "Extrasyst: %s", signal->extrasystolies);
            raw_count++;
            
            snprintf(raw_info[raw_count], sizeof(raw_info[0]), "Size: %d rows x %d cols", signal->rows, signal->cols);
            raw_count++;
            
            // Wrap all lines to 128 pixels max
            char wrapped_buffer[30][64];  // Max 30 wrapped lines total
            uint8_t total_wrapped_lines = 0;
            
            for (uint8_t i = 0; i < raw_count && total_wrapped_lines < 30; i++) {
                char section_wrapped[20][64];
                uint8_t section_lines = word_wrap_text(raw_info[i], section_wrapped, 20, 120);
                
                // Add wrapped lines from this section
                for (uint8_t j = 0; j < section_lines && total_wrapped_lines < 30; j++) {
                    strncpy(wrapped_buffer[total_wrapped_lines], section_wrapped[j], 63);
                    wrapped_buffer[total_wrapped_lines][63] = '\0';
                    total_wrapped_lines++;
                }
            }
            
            // Calculate max pages (5 lines per page, leaving room for nav hint)
            const uint8_t lines_per_page = 5;
            file_info_max_pages = (total_wrapped_lines + lines_per_page - 1) / lines_per_page;
            
            // Clamp current page
            if (file_info_page >= file_info_max_pages) {
                file_info_page = file_info_max_pages - 1;
            }
            
            // Draw lines for current page
            uint8_t start_line = file_info_page * lines_per_page;
            uint8_t end_line = (start_line + lines_per_page < total_wrapped_lines) ? (start_line + lines_per_page) : total_wrapped_lines;
            
            int y_pos = 8;
            for (uint8_t i = start_line; i < end_line; i++) {
                u8g2_DrawStr(&u8g2, 0, y_pos, wrapped_buffer[i]);
                y_pos += 10;
            }
            
            // Draw navigation info at bottom
            char nav_buffer[64];
            if (file_info_max_pages > 1) {
                snprintf(nav_buffer, sizeof(nav_buffer), "Pg %d/%d - Enc:Nav Back:Ret", file_info_page + 1, file_info_max_pages);
            } else {
                snprintf(nav_buffer, sizeof(nav_buffer), "Press Enc to return");
            }
            u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);  // Smaller font for navigation
            u8g2_DrawStr(&u8g2, 0, 63, nav_buffer);
            
            // Send buffer to display
            u8g2_SendBuffer(&u8g2);
            
            // Restore font for GEM
            u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
        }
    } else {
        // Reset pagination when leaving file info page
        if (menu_gem.current_page != &page_file_info) {
            file_info_page = 0;
        }
        
        // Normal menu drawing for other pages
        gem_draw(&menu_gem);

        // --- Draw System Info at Top ---
        u8g2_SetDrawColor(&u8g2, 1);
        u8g2_SetFont(&u8g2, u8g2_font_6x10_tf); // Revert to standard font

        // 1. Battery and USB
        float vbatt = interface_get_battery_voltage();
        bool usb = interface_is_usb_connected();
        char sys_info[64];
        snprintf(sys_info, sizeof(sys_info), "%.1fV%s", vbatt, usb ? "[USB]" : ""); // Shortened [USB] to [U] for more space
        u8g2_DrawStr(&u8g2, 0, 9, sys_info);

        // 1.1 Speaker Icon
        int speaker_x = u8g2_GetStrWidth(&u8g2, sys_info) + 5;
        u8g2_SetFont(&u8g2, u8g2_font_open_iconic_play_1x_t);
        u8g2_DrawGlyph(&u8g2, speaker_x, 9, settings.buzzer_enabled ? 79 : 81); 
        u8g2_SetFont(&u8g2, u8g2_font_6x10_tf); // Restore font

        // 1.2 BPM Display
        uint16_t bpm = signal_gen_get_bpm();
        snprintf(sys_info, sizeof(sys_info), "%dBPM", bpm);
        u8g2_DrawStr(&u8g2, speaker_x + 14, 9, sys_info);

        // 2. Signal Info
        signal_type_t type = signal_gen_get_signal_type();
        const char* type_str = "NONE";
        switch (type) {
            case SIGNAL_TYPE_ECG:      type_str = "CARD"; break;
            case SIGNAL_TYPE_SINE:     type_str = "SINE"; break;
            case SIGNAL_TYPE_TRIANGLE: type_str = "TRIA"; break;
            case SIGNAL_TYPE_SQUARE:   type_str = "SQUA"; break;
        }
        
        snprintf(sys_info, sizeof(sys_info), "%s", type_str);
        // Calculate X position to align right-ish or just fixed
        u8g2_DrawStr(&u8g2, 128 - u8g2_GetStrWidth(&u8g2, sys_info), 9, sys_info);

        // Separator line for system info
        u8g2_DrawHLine(&u8g2, 0, 11, 128);

        // Send finished buffer to display
        u8g2_SendBuffer(&u8g2);
    }
}
bool menu_is_on_file_info_page(void) {
    return menu_gem.current_page == &page_file_info;
}
