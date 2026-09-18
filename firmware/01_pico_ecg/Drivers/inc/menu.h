#ifndef MENU_H
#define MENU_H

#include "gem_core.h"

// ==================== Menu Structures ====================
typedef struct {
    // Signal settings
    uint8_t ecg_type;           // 0=Normal, 1=Synthetic, 2=Recorded
    uint8_t pathology;          // 0=None, 1=Bradycardia, 2=Tachycardia, 3=AFib, 4=PVC, 5=VT
    uint16_t heart_rate;        // 30-240 bpm
    uint16_t amplitude;         // 0-100 % (100 = Current full scale/nominal)
    uint8_t noise_level;        // 0=Off, 1=Muscle, 2=Baseline, 3=50/60Hz

    // Playback settings
    bool is_playing;            // Playing or paused
    bool loop_enabled;          // Loop or one-shot
    
    // Output settings
    uint16_t gain;              // Fixed-point: 100 = 1.0x
    int16_t offset;             // mV
    uint8_t lead;               // 0=I, 1=II, 2=III

    // Interface settings
    uint8_t led_mode;           // 0=ECG Blink, 1=Always On, 2=Off
    bool buzzer_enabled;        // Buzzer on/off
    bool led_enabled;           // LED on/off
    uint8_t encoder_mode;       // 0=Normal, 1=Fast, 2=Inverted

    // Files
    uint8_t selected_file;      // Selected file index

    // Display state
    bool show_diagnostics;      // For diagnostics page
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
bool menu_is_on_file_info_page(void);

#endif // MENU_H
