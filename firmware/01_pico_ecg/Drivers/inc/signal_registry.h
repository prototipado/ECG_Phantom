#ifndef SIGNAL_REGISTRY_H
#define SIGNAL_REGISTRY_H

#include <stdint.h>
#include "ecg_types.h"

// ==================== Signal Registry ====================
// Central registry of all available ECG signals
// To add a new signal:
//  1. Add #include in signal_registry.c
//  2. Add entry to signal_registry array
//  3. Update NUM_SIGNALS define

// Maximum number of signals that can be registered
#define MAX_SIGNALS 16

// Signal metadata for menu display
typedef struct {
    const char* filename;           // e.g., "Atrial_fibrillation_N_51"
    const ecg_struct_t* data;       // Pointer to signal data structure
} signal_entry_t;

// Get the registry of all available signals
const signal_entry_t* signal_registry_get_all(void);

// Get number of available signals
uint8_t signal_registry_get_count(void);

// Get signal by index
const ecg_struct_t* signal_registry_get_by_index(uint8_t index);

// Get signal filename by index
const char* signal_registry_get_filename(uint8_t index);

#endif // SIGNAL_REGISTRY_H
