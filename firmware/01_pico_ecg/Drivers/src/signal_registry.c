#include "signal_registry.h"
#include <stdint.h>
#include <stddef.h>
#include "../../Signals/Atrial_fibrillation_N_51.h"
#include "../../Signals/Atrial_fibrillation_N_38.h"
#include "../../Signals/Atrial_fibrillation_N_8.h"
#include "../../Signals/Atrial_flutter_typical_N_103.h"
#include "../../Signals/Atrial_flutter_typical_N_35.h"
#include "../../Signals/Sinus_arrhythmia_N_99.h"
#include "../../Signals/Sinus_arrhythmia_N_22.h"
#include "../../Signals/Sinus_bradycardia_N_47.h"
#include "../../Signals/Sinus_bradycardia_N_24.h"
#include "../../Signals/Sinus_bradycardia_N_27.h"
#include "../../Signals/Sinus_rhythm_N_16.h"
#include "../../Signals/Sinus_rhythm_N_11.h"
#include "../../Signals/Sinus_rhythm_N_121.h"
#include "../../Signals/Sinus_rhythm_N_17.h"
#include "../../Signals/Sinus_rhythm_N_81.h"
#include "../../Signals/Sinus_rhythm_N_9.h"

// ==================== Signal Registry ====================
// Central registry of all available ECG signals
// Add new signals here - both the include and the registry entry

static const signal_entry_t registry[MAX_SIGNALS] = {
    // Format: { "filename", &signal_structure }
    { "Atrial_fib_51", &Atrial_fibrillation_N_51 },
    { "Atrial_fib_38", &Atrial_fibrillation_N_38 },
    { "Atrial_fib_8", &Atrial_fibrillation_N_8 },
    { "Atrial_flutter_103", &Atrial_flutter_typical_N_103 },
    { "Atrial_flutter_35", &Atrial_flutter_typical_N_35 },
    { "Sinus_arrhy_99", &Sinus_arrhythmia_N_99 },
    { "Sinus_arrhy_22", &Sinus_arrhythmia_N_22 },
    { "Sinus_brady_47", &Sinus_bradycardia_N_47 },
    { "Sinus_brady_24", &Sinus_bradycardia_N_24 },
    { "Sinus_brady_27", &Sinus_bradycardia_N_27 },
    { "Sinus_rhythm_16", &Sinus_rhythm_N_16 },
    { "Sinus_rhythm_11", &Sinus_rhythm_N_11 },
    { "Sinus_rhythm_121", &Sinus_rhythm_N_121 },
    { "Sinus_rhythm_17", &Sinus_rhythm_N_17 },
    { "Sinus_rhythm_81", &Sinus_rhythm_N_81 },
    { "Sinus_rhythm_9", &Sinus_rhythm_N_9 }
    // Add more signals here as needed
};

// Number of signals currently registered
static const uint8_t num_signals = 16;  // Update this when adding signals

// ==================== Public Functions ====================

const signal_entry_t* signal_registry_get_all(void) {
    return registry;
}

uint8_t signal_registry_get_count(void) {
    return num_signals;
}

const ecg_struct_t* signal_registry_get_by_index(uint8_t index) {
    if (index >= num_signals) return NULL;
    return registry[index].data;
}

const char* signal_registry_get_filename(uint8_t index) {
    if (index >= num_signals) return "UNKNOWN";
    return registry[index].filename;
}
