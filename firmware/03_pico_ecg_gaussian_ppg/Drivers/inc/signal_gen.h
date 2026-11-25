#ifndef SIGNAL_GEN_H
#define SIGNAL_GEN_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SIGNAL_TYPE_ECG,      // Gaussian event-driven ECG+PPG model
    SIGNAL_TYPE_SINE,
    SIGNAL_TYPE_TRIANGLE,
    SIGNAL_TYPE_SQUARE,
    SIGNAL_TYPE_PULSE
} signal_type_t;

// Initialization
void signal_gen_init(void);

// Core 1 Entry Point
void signal_gen_core1_entry(void);

// Start/Stop generation
void signal_gen_start(void);
void signal_gen_stop(void);

// Set signal type
void signal_gen_set_signal_type(signal_type_t type);

// Set waveform parameters
void signal_gen_set_amplitude(uint16_t amplitude);
void signal_gen_set_frequency(uint16_t frequency);
void signal_gen_set_bpm(uint16_t bpm);
void signal_gen_set_offset(int16_t offset);  // Vertical offset
bool signal_gen_calibrate_startup(void);

// Output calibration gain (counts per mV multiplier, default 1.0).
// Scales the ECG model mV -> PWM counts conversion so a known reference
// signal can be matched to the real output on an oscilloscope.
void signal_gen_set_cal_gain(float gain);
float signal_gen_get_cal_gain(void);

// Get current signal type and metrics
signal_type_t signal_gen_get_signal_type(void);
uint16_t signal_gen_get_bpm(void);

// PPG output scale (0..1.0). Reduces PPG amplitude to avoid clipping.
void signal_gen_set_ppg_scale(float scale);
float signal_gen_get_ppg_scale(void);


#endif // SIGNAL_GEN_H
