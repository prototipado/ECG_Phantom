#ifndef GAUSSIAN_ECG_PPG_MODEL_H
#define GAUSSIAN_ECG_PPG_MODEL_H

#include <stdint.h>
#include <stdbool.h>

// Event-driven Gaussian ECG + PPG model, faithful to:
//   Huynh, Tran & Tran, "An ECG-PPG Physiological Signal Emulator for
//   Calibration and Validation of Cardiovascular Monitoring Devices",
//   Sensors 2026, 26, 5578.
//
// The implementation is split into three logical layers (see the .c file
// for the full backing math):
//   L1 physiology: independent atrial (t_k^A) and ventricular (t_j^V) event
//     sequences (eq. 1); AV conduction flag c_k and PR_k per atrial event
//     (eq. 2); beat class (normal/ectopic) and RR bookkeeping.
//   L2 math: the extended 3-D cardiac vector v(t)=[X,Y,Z] built as a sum of
//     Gaussian components (eq. 3); electrode projection phi_e = p_e . v(t)
//     (eq. 4); the 12-lead reconstruction from the 9 electrode potentials
//     (eq. 6, Einthoven + Wilson central terminal); the T-wave rate
//     adaptation (eq. 5) and the 5-Gaussian peripheral pulse (eq. 7-8).
//   L3 hardware: stays in signal_gen.c (electrode -> PWM mapping, DAC
//     calibration, streaming) and only consumes the 9 electrode potentials
//     produced here plus the raw PPG value.
//
// Unit conventions (conversions centralized in this file):
//   simulated time is kept in seconds internally, Gaussian parameters and
//   intervals (PR, RR, PEP, PAT, mu, sigma) are in ms, HR in bpm, PWV in
//   m/s and vascular path length L in m.
//
// Timing conventions (important):
//   Atrial events fire at t_k^A. The P wave is rendered at t_k^A + mu_P
//   with mu_P >= 0 (P peak shortly after atrial activation), so there is no
//   accidental extra shift between P and R. Conducted atrial events set
//   t_j^V = t_k^A + PR_k, hence the *measured* PR interval equals PR_k in
//   ms directly. Q, R, S, T are rendered relative to their ventricular
//   event with mu_R = 0, i.e. the event time coincides with the R peak.
#define GAUSSIAN_MODEL_NUM_LEADS 9      // electrodes: RA, LA, LL, V1 ... V6
#define GAUSSIAN_MODEL_NUM_LEADS_12 12  // leads: I, II, III, aVR, aVL, aVF, V1 ... V6

typedef struct {
    // ---- L1: event schedule (eq. 1) ----
    float atrial_rate_bpm;      // mean atrial event rate
    float resp_mod_depth;       // alphaA/alphaV: respiratory modulation depth
    float resp_freq_hz;         // fr: respiratory/RSA modulation frequency

    // ---- L1: atrioventricular conduction (eq. 2) ----
    float pr_interval_ms;       // PR0: baseline AV conduction interval
    float pr_increment_ms;      // Mobitz I: PR prolongation per conducted beat
    uint8_t wenckebach_beats;   // Mobitz I: conducted beats before a dropped beat
    float escape_rate_bpm;      // independent ventricular rate (block / VT)

    // ---- L2: Gaussian ECG kernel parameters (eq. 3) ----
    // Offsets/widths are measured in ms from the triggering atrial/ventricular
    // event; amplitudes in model units (mV-equivalent after DAC calibration).
    float p_amp, p_mu_ms, p_sigma_ms;      // P from the atrial event, mu >= 0
    float q_amp, q_mu_ms, q_sigma_ms;
    float r_amp, r_sigma_ms;               // R centered on the ventricular event (mu=0)
    float s_amp, s_mu_ms, s_sigma_ms;
    float t_amp, t_mu_ms, t_sigma_ms;      // base (rate 0) values; see eq. 5

    // ---- L2: T-wave rate adaptation (eq. 5): fT = clip((RR_j/RR0)^alphaQT) ----
    float qt_rate_exponent;
    float qt_scale_min, qt_scale_max;
    float rr0_ms;               // reference RR interval for the T scaling law

    // ---- L1/L2: ectopic (ventricular tachycardia) beat class ----
    float ectopic_qrs_widen;    // multiplies Q/R/S sigma for ectopic beats
    float ectopic_qrs_delay_ms; // shifts Q/R/S mu for ectopic beats
    float ectopic_pep_ms;       // extra pre-ejection delay for ectopic beats

    // ---- L2: PPG coupling (eq. 7-8) ----
    float pep_ms;               // pre-ejection period
    float path_length_m;        // effective vascular path length L
    float pwv_mps;              // pulse wave velocity
    float ectopic_ppg_amp_scale; // amplitude scale for ectopic beats
} gaussian_model_params_t;

extern gaussian_model_params_t gaussian_model_params;

// ---- L1: physiology types (public for validation layers / tests) ----

typedef enum {
    BEAT_NORMAL = 0,
    BEAT_ECTOPIC = 1
} beat_class_t;

typedef struct {
    float time_s;        // ventricular activation time (= R-peak instant), s
    float rr_ms;         // preceding RR interval, ms
    float t_scale;       // fT for this beat (T-wave rate adaptation, eq. 5)
    beat_class_t cls;
} ventricular_event_t;

typedef struct {
    float time_s;        // atrial activation time t_k^A, s
    float pr_ms;         // AV conduction interval for this event, ms
    bool conducted;      // c_k
} atrial_event_t;

typedef struct {
    float time_s;        // pulse-foot (arrival) time t_F (eq. 7), s
    float amplitude;     // amplitude scale (ectopic reduction)
} ppg_event_t;

// ---- L2: vector math types ----

typedef struct { float x, y, z; } cardiac_vector_t;    // v(t) = [X, Y, Z]
typedef struct { float x, y, z; } projection_vector_t; // p_e (unit direction to electrode)

typedef enum {
    GAUSSIAN_RHYTHM_NORMAL_SINUS = 0,
    GAUSSIAN_RHYTHM_FIRST_DEGREE_BLOCK,
    GAUSSIAN_RHYTHM_MOBITZ_I,
    GAUSSIAN_RHYTHM_COMPLETE_BLOCK,
    GAUSSIAN_RHYTHM_ATRIAL_TACHYCARDIA,
    GAUSSIAN_RHYTHM_VENTRICULAR_TACHYCARDIA,
    GAUSSIAN_RHYTHM_COUNT
} gaussian_rhythm_t;

const char* gaussian_model_get_rhythm_name(uint8_t index);
uint8_t gaussian_model_get_rhythm_count(void);
void gaussian_model_apply_rhythm(uint8_t index);
uint8_t gaussian_model_get_current_rhythm(void);

void gaussian_model_init(float sample_rate_hz);
void gaussian_model_set_heart_rate(float bpm);
// Restores the factory boot configuration (all parameters + Normal Sinus).
void gaussian_model_reset_defaults(void);

// Advances the model by one sample at the current simulated time. Fills
// ecg_out[GAUSSIAN_MODEL_NUM_LEADS] with the electrode potentials
// (RA, LA, LL, V1..V6) in mV-equivalent model units and returns the PPG
// sample (0..~1 a.u., may exceed 1 while overlapping pulses merge).
float gaussian_model_step(float* ecg_out);

// Reconstructs the 12 standard ECG leads from the 9 electrode potentials
// produced by gaussian_model_step (eq. 6). electrodes[9] order is
// RA, LA, LL, V1..V6; leads[12] order is I, II, III, aVR, aVL, aVF, V1..V6.
void gaussian_model_compute_12_leads(const float* electrodes, float* leads);

// Hardware output mapping (eq. 4/6 composed with the board wiring). The RA
// electrode pin (GPIO0) is re-purposed as the full-swing PPG output, so the
// RA potential is not driven on any pin and the device under test (DUT) must
// tie its RA input to GND. outputs[9] then holds the values the ECG pins
// must emit so that a DUT with grounded RA measures the model's 12 leads
// EXACTLY:
//   outputs[0] = phi_RA           (reference; streamed only, no pin)
//   outputs[e] = phi_e - phi_RA   (e = 1..8: GPIO1 = lead I, GPIO2 = lead II,
//                                  GPIO3..9 = V1..V6)
// Derivation: with V_RA = 0 the DUT's Wilson terminal is
// WCT = (0 + I + II)/3 = -phi_RA (the model's limb projection vectors sum to
// zero, so the model's own WCT = 0), hence Vk_dut = (phi_k - phi_RA) -
// (-phi_RA) = phi_k, the model precordial lead. Verified per rhythm by the
// self test (gaussian_model_self_test).
void gaussian_model_compute_output_electrodes(const float* electrodes, float* outputs);

// Validation / telemetry helpers.
float gaussian_model_get_last_rr_ms(void);            // RR of the last ventricular event
float gaussian_model_get_last_pr_ms(void);            // last conducted AV interval
void gaussian_model_get_einthoven_error(float* err_iii, float* err_wilson);

// Runs a deterministic validation pass over every rhythm preset (3 s each at
// the model sample rate), checking the measured PR/RR against the paper's
// values and the Einthoven/Wilson constraints. Must be called while the
// generator is stopped (e.g. at boot before streaming, or from a debug
// console). Returns true when all checks pass. To run it automatically at
// boot, define GAUSSIAN_MODEL_SELF_TEST_RUN_AT_BOOT in the build; it then
// also prints a summary line over stdout.
bool gaussian_model_self_test(void);

#endif // GAUSSIAN_ECG_PPG_MODEL_H