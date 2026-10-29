#ifndef CARDIAC_MODEL_H
#define CARDIAC_MODEL_H

#include <stdint.h>
#include <stdbool.h>

// Number of electrode outputs produced by the model, in the same order used
// by signal_gen/PWM mapping: [0]=RA(ref,always 0), [1]=LA(=Lead I), [2]=LL(=Lead II),
// [3..8] = V1..V6
#define CARDIAC_MODEL_NUM_LEADS 9

// ==================== Editable model parameters ====================
// These mirror the equations of:
// Quiroz-Juarez et al., "Cardiac Conduction Model for Generating 12 Lead ECG
// Signals With Realistic Heart Rate Dynamics", IEEE Trans. Nanobioscience, 2018.
typedef struct {
    // Natural pacemakers (modified Van der Pol oscillators), eq. (1)
    float a1, a2, a3;      // damping coefficients (SA, AV, HP)
    float f2, f3;          // intrinsic frequency parameters of AV and HP (SA uses f1, derived from heart rate)
    float u;               // damping shape parameter (shared)
    float d;               // harmonic force shared parameter
    float e1, e2, e3;      // harmonic force parameters (SA, AV, HP)
    float tau_node_ms;     // coupling transport delay (ms) between nodes

    // AT/VN muscles (modified FitzHugh-Nagumo), eq. (2)-(3), weighting eq. (6)
    float alpha1, alpha2, alpha3, alpha4; // P, Ta, QRS, T weighting coefficients

    // Heart rate variability / baseline wander, eq. (4)-(9)
    float hrv_depth;       // 0..1, relative modulation depth of f1 (RSA + Mayer approx.)
    float baseline_z0_mV;  // baseline wander amplitude
    float baseline_f0_hz;  // baseline wander (respiratory) frequency

    bool hrv_enabled;
    bool baseline_wander_enabled;
} cardiac_model_params_t;

extern cardiac_model_params_t cardiac_model_params;

// ==================== Rhythm presets ====================
typedef enum {
    CARDIAC_RHYTHM_NORMAL = 0,
    CARDIAC_RHYTHM_SINUS_BRADYCARDIA,
    CARDIAC_RHYTHM_SINUS_TACHYCARDIA,
    CARDIAC_RHYTHM_SA_AV_BLOCK,
    CARDIAC_RHYTHM_AV_HP_BLOCK,
    CARDIAC_RHYTHM_WELLENS_V1V3,
    CARDIAC_RHYTHM_COUNT
} cardiac_rhythm_t;

const char* cardiac_model_get_rhythm_name(uint8_t index);
uint8_t cardiac_model_get_rhythm_count(void);
void cardiac_model_apply_rhythm(uint8_t index);
uint8_t cardiac_model_get_current_rhythm(void);

// ==================== Core API ====================
void cardiac_model_init(float sample_rate_hz);
void cardiac_model_set_heart_rate(float bpm);

// Advances the model by one sample and writes CARDIAC_MODEL_NUM_LEADS
// electrode potentials (mV, roughly -2..+3 mV range) into `out`.
void cardiac_model_step(float* out);

#endif // CARDIAC_MODEL_H
