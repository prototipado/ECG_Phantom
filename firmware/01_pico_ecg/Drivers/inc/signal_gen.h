#ifndef SIGNAL_GEN_H
#define SIGNAL_GEN_H

#include "ecg_types.h"
#include <stdint.h>

typedef enum {
    SIGNAL_TYPE_ECG,
    SIGNAL_TYPE_SINE,
    SIGNAL_TYPE_TRIANGLE,
    SIGNAL_TYPE_SQUARE,
    SIGNAL_TYPE_PULSE
} signal_type_t;

// Initialization
void signal_gen_init(void);

// Core 1 Entry Point
void signal_gen_core1_entry(void);

// Set active signal
void signal_gen_set_signal(const ecg_struct_t* signal);

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

// Get current signal type and metrics
signal_type_t signal_gen_get_signal_type(void);
uint16_t signal_gen_get_bpm(void);

#endif // SIGNAL_GEN_H
