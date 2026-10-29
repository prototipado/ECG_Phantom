#ifndef MENU_H
#define MENU_H

#include "gem_core.h"

// ==================== Menu Structures ====================
typedef struct {
    // Signal settings
    uint16_t heart_rate;        // 15-250 bpm
    uint16_t amplitude;         // 0-100 % (100 = Current full scale/nominal)
    int16_t offset;             // Vertical offset

    // Playback settings
    bool is_playing;            // Playing or paused

    // Rhythm preset (index into cardiac_model rhythm table)
    uint8_t rhythm_index;

    // Interface settings
    bool buzzer_enabled;        // Buzzer on/off
    bool led_enabled;           // LED on/off
} ecg_settings_t;

// Global settings
extern ecg_settings_t settings;
extern gem_t menu_gem;

// ==================== Initialization ====================
void menu_init(void);

// ==================== Input Processing ====================
void menu_process_input(void);

// ==================== Drawing ====================
void menu_draw(void);

#endif // MENU_H
