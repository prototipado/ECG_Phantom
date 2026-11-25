// Event-driven Gaussian ECG + PPG model, faithful to Huynh, Tran & Tran,
// "An ECG-PPG Physiological Signal Emulator for Calibration and Validation
// of Cardiovascular Monitoring Devices", Sensors 2026, 26, 5578.
//
// Implementation organized in three logical layers (see header):
//
//   L1 physiology/events:
//     - Independent atrial (t_k^A) and ventricular (t_j^V) event sequences
//       with respiratory modulation (eq. 1).
//     - Per-event AV conduction flag c_k and PR_k (eq. 2). Normal sinus,
//       first-degree block (PR=240 ms), Mobitz I with an explicit counter
//       (PR0=160 ms, +40 ms per conducted beat, then one dropped beat and a
//       reset), complete AV block (atrial 75 bpm, escape 38 bpm) and
//       ventricular tachycardia (160 bpm, ectopic beats) reproduce the
//       paper's validated rhythms.
//     - Active events are kept in small circular buffers so overlapping
//       P/QRS/T/PPG components from consecutive beats merge correctly.
//       Ventricular events are inserted QRS_PRE_RENDER_MS before their
//       true instant and atrial events P_PRE_RENDER_MS before theirs, so the
//       R upstroke and the P onset ramp up from baseline; telemetry (PR/RR,
//       PPG foot) stays pinned to the event instants.
//   L2 math:
//     - 3-D cardiac vector v(t) = [X, Y, Z] (eq. 3), built as a sum of
//       Gaussian components, each with a fixed unit direction e_m and a
//       user-editable scalar amplitude:
//         v(t) = sum_k a_P e_P g_P(t - t_k^A - mu_P)
//              + sum_j sum_m a_m e_m g_m(t - t_j^V - mu_m),  m in {Q,R,S,T}
//       with the Gaussian kernel g_m(tau) = exp(-tau^2 / (2 sigma_m^2)).
//     - Electrode projection (eq. 4): phi_e(t) = p_e . v(t), with p_e the
//       unit vector from the cardiac dipole to electrode e. The limb
//       electrodes (RA, LA, LL) form a closed Einthoven triangle whose
//       vectors sum to zero; the Wilson central terminal WCT is their mean.
//     - 12-lead reconstruction (eq. 6) from the 9 electrodes with the
//       standard Einthoven and augmented formulas (see
//       gaussian_model_compute_12_leads) plus running Einthoven-consistency
//       checks.
//     - T-wave duration follows the beat's own RR interval: mu_T and
//       sigma_T are scaled by fT_j = clip((RR_j/RR0)^alphaQT) (eq. 5).
//     - Peripheral pulse (eq. 7-8): one pulse-foot event per ventricular
//       activation at t_F,j = t_j^V + PEP_j + L/PWV_j; the pulse is a sum
//       of 5 Gaussians (systolic peak, shoulder, dicrotic notch, diastolic
//       peak, tail) with a smooth turn-on envelope. Ectopic beats add an
//       extra PEP and scale the pulse amplitude.
//
// Timing conventions: all Gaussian offsets (mu) are measured from the
// triggering event. mu_P >= 0 so the P peak sits shortly AFTER the atrial
// event; with t_j^V = t_k^A + PR_k the measured PR interval equals PR_k
// directly (no accidental extra 139 ms shift as in earlier revisions).
// Simulated time is stored in seconds; every interval/counter in the model
// is expressed in ms and converted at the boundaries.
#include "gaussian_ecg_ppg_model.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// L1: physiology types and event buffers
// ---------------------------------------------------------------------------

// Active-event capacity. These are enough for the fastest rhythms in the
// model (160 bpm -> ~375 ms > any rendered kernel tail), so an eviction
// strategy is only a safety net.
#define MAX_ACTIVE_ATRIAL_EVENTS       6
#define MAX_ACTIVE_VENTRICULAR_EVENTS  6
#define MAX_ACTIVE_PPG_EVENTS          6

// Render windows (ms of simulation time around each event; kernels outside
// the window contribute < 1e-3 of peak and are skipped for speed).
#define ATRIAL_RENDER_WINDOW_MS 250.0f
// The ventricle window must cover the full T-wave tail. The T apex sits at
// fT * t_mu (up to fT_max=1.3 * 208.3 ~ 271 ms) and the kernel tail extends
// to fT * (t_mu + ~4*sigma) ~ 581 ms; a shorter window clipped the T mid-
// rise and produced an abrupt drop back to baseline. 
#define VENT_RENDER_WINDOW_MS   650.0f
#define PPG_RENDER_WINDOW_MS    1100.0f
// Start drawing each QRS this many ms before the ventricular event so the Q
// wave and the rising edge of R (tau < 0) are rendered; the events are
// inserted into the render buffer ahead of time while the physiology
// bookkeeping stays pinned to t_v (keeps PR/RR telemetry and the PPG foot
// exact). 120 ms fully covers the ectopic Q (mu -34.7 - 25 ms, sigma ~24).
#define QRS_PRE_RENDER_MS       120.0f
// Same idea for the P wave: its kernel peaks at +45 ms and is non-negligible
// back to ~ -100 ms, so each atrial event is inserted this many ms before
// t_k^A and the P render window starts that far to the left. Without it the
// P began with a vertical step of up to ~0.5 mV (sign per electrode).
#define P_PRE_RENDER_MS         120.0f
#define LEAD_FIRST_MS           -10.0f   // pulse-foot turn-on lead time

static atrial_event_t          atrial_events[MAX_ACTIVE_ATRIAL_EVENTS];
static ventricular_event_t     vent_events[MAX_ACTIVE_VENTRICULAR_EVENTS];
static ppg_event_t             ppg_events[MAX_ACTIVE_PPG_EVENTS];
static uint8_t                 num_atrial_events = 0;
static uint8_t                 num_vent_events = 0;
static uint8_t                 num_ppg_events = 0;

static void push_atrial_event(float time_s, float pr_ms, bool conducted) {
    if (num_atrial_events >= MAX_ACTIVE_ATRIAL_EVENTS) {
        memmove(&atrial_events[0], &atrial_events[1],
                (MAX_ACTIVE_ATRIAL_EVENTS - 1) * sizeof(atrial_event_t));
        num_atrial_events = MAX_ACTIVE_ATRIAL_EVENTS - 1;
    }
    atrial_events[num_atrial_events].time_s = time_s;
    atrial_events[num_atrial_events].pr_ms = pr_ms;
    atrial_events[num_atrial_events].conducted = conducted;
    num_atrial_events++;
}

static void push_vent_event(float time_s, float rr_ms, float t_scale, beat_class_t cls) {
    if (num_vent_events >= MAX_ACTIVE_VENTRICULAR_EVENTS) {
        memmove(&vent_events[0], &vent_events[1],
                (MAX_ACTIVE_VENTRICULAR_EVENTS - 1) * sizeof(ventricular_event_t));
        num_vent_events = MAX_ACTIVE_VENTRICULAR_EVENTS - 1;
    }
    vent_events[num_vent_events].time_s = time_s;
    vent_events[num_vent_events].rr_ms = rr_ms;
    vent_events[num_vent_events].t_scale = t_scale;
    vent_events[num_vent_events].cls = cls;
    num_vent_events++;
}

static void push_ppg_event(float time_s, float amplitude) {
    if (num_ppg_events >= MAX_ACTIVE_PPG_EVENTS) {
        memmove(&ppg_events[0], &ppg_events[1],
                (MAX_ACTIVE_PPG_EVENTS - 1) * sizeof(ppg_event_t));
        num_ppg_events = MAX_ACTIVE_PPG_EVENTS - 1;
    }
    ppg_events[num_ppg_events].time_s = time_s;
    ppg_events[num_ppg_events].amplitude = amplitude;
    num_ppg_events++;
}

// ---------------------------------------------------------------------------
// L2: math constants
// ---------------------------------------------------------------------------

// Cardiac-vector component directions e_m (unit vectors in [X, Y, Z], where
// X = patient left, Y = feet, Z = anterior). These reproduce the classical
// morphology: upright P/T and dominant R in the inferior and left
// precordial leads, deep S in the right precordial leads, and a
// right-side / anterior S axis.
typedef struct { float x, y, z; } vec3;

static const vec3 WAVE_DIR_P = { 0.329f, 0.940f, 0.094f };
static const vec3 WAVE_DIR_Q = { 0.100f, 0.300f, -0.949f };
static const vec3 WAVE_DIR_R = { 0.470f, 0.813f, 0.341f };
static const vec3 WAVE_DIR_S = { -0.150f, 0.250f, 0.957f };
static const vec3 WAVE_DIR_T = { 0.432f, 0.864f, 0.259f };

// Electrode projection vectors p_e (unit vectors from the cardiac dipole to
// each electrode). The limb electrodes are the vertices of the Einthoven
// triangle and satisfy p_RA + p_LA + p_LL = 0, so the Wilson central
// terminal WCT = (p_RA + p_LA + p_LL)/3 = 0 keeps the precordial leads
// referenced to a (near-)zero potential.
static const vec3 ELECTRODE_PROJECTION[GAUSSIAN_MODEL_NUM_LEADS] = {
    /* RA */ { -0.86603f, -0.50000f,  0.00000f },
    /* LA */ {  0.86603f, -0.50000f,  0.00000f },
    /* LL */ {  0.00000f,  1.00000f,  0.00000f },
    /* V1 */ {  0.00000f,  0.00000f,  1.00000f },
    /* V2 */ {  0.14000f,  0.00000f,  0.99015f },
    /* V3 */ {  0.37000f,  0.00000f,  0.92902f },
    /* V4 */ {  0.60000f,  0.00000f,  0.80000f },
    /* V5 */ {  0.87000f,  0.00000f,  0.49300f },
    /* V6 */ {  0.98000f,  0.00000f,  0.19600f },
};

// Per-electrode projection of each wave direction: p_e . e_m.
static float DOT_P[GAUSSIAN_MODEL_NUM_LEADS];
static float DOT_Q[GAUSSIAN_MODEL_NUM_LEADS];
static float DOT_R[GAUSSIAN_MODEL_NUM_LEADS];
static float DOT_S[GAUSSIAN_MODEL_NUM_LEADS];
static float DOT_T[GAUSSIAN_MODEL_NUM_LEADS];

static void init_projection_tables(void) {
    for (int e = 0; e < GAUSSIAN_MODEL_NUM_LEADS; e++) {
        DOT_P[e] = ELECTRODE_PROJECTION[e].x * WAVE_DIR_P.x +
                   ELECTRODE_PROJECTION[e].y * WAVE_DIR_P.y +
                   ELECTRODE_PROJECTION[e].z * WAVE_DIR_P.z;
        DOT_Q[e] = ELECTRODE_PROJECTION[e].x * WAVE_DIR_Q.x +
                   ELECTRODE_PROJECTION[e].y * WAVE_DIR_Q.y +
                   ELECTRODE_PROJECTION[e].z * WAVE_DIR_Q.z;
        DOT_R[e] = ELECTRODE_PROJECTION[e].x * WAVE_DIR_R.x +
                   ELECTRODE_PROJECTION[e].y * WAVE_DIR_R.y +
                   ELECTRODE_PROJECTION[e].z * WAVE_DIR_R.z;
        DOT_S[e] = ELECTRODE_PROJECTION[e].x * WAVE_DIR_S.x +
                   ELECTRODE_PROJECTION[e].y * WAVE_DIR_S.y +
                   ELECTRODE_PROJECTION[e].z * WAVE_DIR_S.z;
        DOT_T[e] = ELECTRODE_PROJECTION[e].x * WAVE_DIR_T.x +
                   ELECTRODE_PROJECTION[e].y * WAVE_DIR_T.y +
                   ELECTRODE_PROJECTION[e].z * WAVE_DIR_T.z;
    }
    (void)0;
}

// Fixed 5-Gaussian PPG morphology template (eq. 8): systolic peak, systolic
// shoulder, dicrotic notch region, diastolic peak and tail. Not exposed in
// the menu to keep the UI compact; documented in the README.
typedef struct { float a, mu_ms, sigma_ms; } ppg_component_t;
static const ppg_component_t PPG_TEMPLATE[5] = {
    {1.00f, 120.0f, 45.0f},  // systolic peak
    {0.35f, 180.0f, 50.0f},  // systolic shoulder
    {0.25f, 300.0f, 60.0f},  // dicrotic notch region
    {0.15f, 400.0f, 80.0f},  // diastolic peak
    {0.08f, 550.0f, 100.0f}, // tail
};

// ---------------------------------------------------------------------------
// Model state (L1 schedule + telemetry)
// ---------------------------------------------------------------------------

gaussian_model_params_t gaussian_model_params;

static float dt = 1.0f / 2000.0f;
static float t_sim = 0.0f;
static uint8_t current_rhythm = GAUSSIAN_RHYTHM_NORMAL_SINUS;

static float next_atrial_time_s = 0.05f;
static float next_independent_vent_time_s = 1.0e9f; // autonomous V schedule (block/VT)
static float last_ventricular_time_s = -1.0f;
static float last_rr_ms = 833.0f;   // 72 bpm reference
static float last_rendered_vent_time_s = -1.0e9f; // guards QRS pre-render insertion
static float last_rendered_atrial_time_s = -1.0e9f; // guards P pre-render insertion

// Conducted ventricular events are queued (FIFO) by their scheduled times.
// A scalar could never hold more than one pending ventricular event, which
// would silently drop the second one in a fast 1:1-conducting rhythm
// (e.g. atrial tachycardia at 160 bpm would halve to 80 bpm).
#define MAX_ACTIVE_PENDING_VENT 4
static float pending_vent_times[MAX_ACTIVE_PENDING_VENT];
static uint8_t pending_vent_head = 0;
static uint8_t pending_vent_count = 0;

static void enqueue_pending_vent(float t_v) {
    if (pending_vent_count >= MAX_ACTIVE_PENDING_VENT) return; // safety: overflow drop
    uint8_t idx = (uint8_t)((pending_vent_head + pending_vent_count) % MAX_ACTIVE_PENDING_VENT);
    pending_vent_times[idx] = t_v;
    pending_vent_count++;
}

static float pending_vent_front(void) { return pending_vent_times[pending_vent_head]; }

static void dequeue_pending_vent(void) {
    if (pending_vent_count == 0) return;
    pending_vent_head = (uint8_t)((pending_vent_head + 1) % MAX_ACTIVE_PENDING_VENT);
    pending_vent_count--;
}

// Mobitz I (Wenckebach) explicit counter: conducted beats since the last
// dropped one. The counted beat is blocked, then the counter resets and PR
// returns to PR0.
static uint8_t mobitz_beats_since_drop = 0;
static float mobitz_pr_current_ms = 160.0f;

// Conduction telemetry: last conducted AV interval (eq. 2, exactly PR_k).
static float last_pr_measured_ms = 160.0f;

// Einthoven-consistency checks (eq. 6), running maxima since model init.
static float einthoven_iii_error = 0.0f;
static float einthoven_wilson_error = 0.0f;

static const char* rhythm_names[GAUSSIAN_RHYTHM_COUNT] = {
    "Normal Sinus",
    "First-Degree AV Block",
    "Mobitz I (Wenckebach)",
    "Complete AV Block",
    "Atrial Tachycardia",
    "Ventricular Tachycardia",
};

// ---------------------------------------------------------------------------
// Unit conversion helpers (centralized)
// ---------------------------------------------------------------------------

static inline float ms_to_s(float ms) { return ms / 1000.0f; }
static inline float s_to_ms(float s) { return s * 1000.0f; }

// Pulse-arrival time (eq. 7): PAT = PEP + L/PWV, all converted to seconds.
static inline float pat_seconds(float pep_ms, float length_m, float pwv_mps) {
    return ms_to_s(pep_ms) + length_m / pwv_mps;
}

static inline float rr_seconds(float bpm) {
    return ms_to_s(60000.0f / bpm);
}

// ---------------------------------------------------------------------------
// Gaussian kernel (all times in ms)
// ---------------------------------------------------------------------------

static inline float gauss_ms(float t_ms, float mu_ms, float sigma_ms, float amp) {
    if (sigma_ms < 0.1f) sigma_ms = 0.1f;
    float d = t_ms - mu_ms;
    return amp * expf(-(d * d) / (2.0f * sigma_ms * sigma_ms));
}

// ---------------------------------------------------------------------------
// Rhythm presets (paper-validated parameter sets)
// ---------------------------------------------------------------------------

const char* gaussian_model_get_rhythm_name(uint8_t index) {
    if (index >= GAUSSIAN_RHYTHM_COUNT) return "?";
    return rhythm_names[index];
}
uint8_t gaussian_model_get_rhythm_count(void) { return GAUSSIAN_RHYTHM_COUNT; }
uint8_t gaussian_model_get_current_rhythm(void) { return current_rhythm; }

static void set_default_params(void) {
    gaussian_model_params_t* p = &gaussian_model_params;
    p->atrial_rate_bpm = 72.0f;
    p->resp_mod_depth = 0.0f;   // eq.1 alphaA/alphaV; 0 = no HRV (paper default)
    p->resp_freq_hz = 0.25f;

    p->pr_interval_ms = 160.0f;
    p->pr_increment_ms = 40.0f; // paper's Mobitz I PR increment
    p->wenckebach_beats = 4;
    p->escape_rate_bpm = 38.0f; // paper's complete-block escape rate

    // Amplitudes chosen so the per-lead ratios match the paper's morphology
    // (T ~ 0.15 of R, P ~ 0.1 of R). An earlier set used r_amp=30 with
    // t_amp=0.75, which made the T wave only ~2% of R and did not reproduce
    // the prominent T seen in the paper's figures.
    p->p_amp = 0.15f; p->p_mu_ms = 45.0f; p->p_sigma_ms = 33.2f;
    p->q_amp = -0.1f; p->q_mu_ms = -34.7f; p->q_sigma_ms = 13.3f;
    p->r_amp = 1.2f;  p->r_sigma_ms = 13.3f;
    p->s_amp = -0.3f; p->s_mu_ms = 34.7f;  p->s_sigma_ms = 13.3f;
    p->t_amp = 0.19f; p->t_mu_ms = 208.3f; p->t_sigma_ms = 59.7f;

    p->qt_rate_exponent = 0.3f;
    p->qt_scale_min = 0.6f;
    p->qt_scale_max = 1.3f;
    p->rr0_ms = 833.0f; // 72 bpm

    p->ectopic_qrs_widen = 1.8f;
    p->ectopic_qrs_delay_ms = 25.0f;
    p->ectopic_pep_ms = 20.0f;

    p->pep_ms = 80.0f;
    p->path_length_m = 0.5f;
    p->pwv_mps = 5.0f;
    p->ectopic_ppg_amp_scale = 0.5f;
}

static void reset_event_state(void) {
    num_atrial_events = 0;
    num_vent_events = 0;
    num_ppg_events = 0;
    pending_vent_head = 0;
    pending_vent_count = 0;
    next_independent_vent_time_s = 1.0e9f;
    last_ventricular_time_s = -1.0f;
    last_rendered_vent_time_s = -1.0e9f;
    last_rendered_atrial_time_s = -1.0e9f;
    last_rr_ms = gaussian_model_params.rr0_ms;
    mobitz_beats_since_drop = 0;
    mobitz_pr_current_ms = gaussian_model_params.pr_interval_ms;
    next_atrial_time_s = t_sim + 0.01f;   // apply rhythm promptly
    if (current_rhythm == GAUSSIAN_RHYTHM_COMPLETE_BLOCK ||
        current_rhythm == GAUSSIAN_RHYTHM_VENTRICULAR_TACHYCARDIA) {
        next_independent_vent_time_s = t_sim + 0.02f;
    }
}

void gaussian_model_apply_rhythm(uint8_t index) {
    if (index >= GAUSSIAN_RHYTHM_COUNT) return;
    current_rhythm = index;
    gaussian_model_params_t* p = &gaussian_model_params;

    switch (index) {
        case GAUSSIAN_RHYTHM_NORMAL_SINUS:
            // Full restore to the boot configuration so selecting Normal
            // Sinus always returns to the factory waveform, regardless of
            // slider/CLI parameter edits made in the meantime.
            set_default_params();
            p = &gaussian_model_params;
            p->atrial_rate_bpm = 72.0f;
            p->pr_interval_ms = 160.0f;
            break;
        case GAUSSIAN_RHYTHM_FIRST_DEGREE_BLOCK:
            p->atrial_rate_bpm = 72.0f;
            p->pr_interval_ms = 240.0f; // prolonged but 1:1 conduction
            break;
        case GAUSSIAN_RHYTHM_MOBITZ_I:
            p->atrial_rate_bpm = 75.0f;
            p->pr_interval_ms = 160.0f; // PR0
            p->pr_increment_ms = 40.0f; // dPR
            break;
        case GAUSSIAN_RHYTHM_COMPLETE_BLOCK:
            p->atrial_rate_bpm = 75.0f;
            p->escape_rate_bpm = 38.0f; // ventricular escape rhythm
            break;
        case GAUSSIAN_RHYTHM_ATRIAL_TACHYCARDIA:
            p->atrial_rate_bpm = 160.0f;
            p->pr_interval_ms = 160.0f;
            break;
        case GAUSSIAN_RHYTHM_VENTRICULAR_TACHYCARDIA:
            p->atrial_rate_bpm = 72.0f; // dissociated atria continue normally
            p->escape_rate_bpm = 160.0f; // fast independent ventricular ectopic rhythm
            break;
    }
    reset_event_state();
}

void gaussian_model_init(float sample_rate_hz) {
    if (sample_rate_hz > 0) dt = 1.0f / sample_rate_hz;
    set_default_params();
    init_projection_tables();
    t_sim = 0.0f;
    einthoven_iii_error = 0.0f;
    einthoven_wilson_error = 0.0f;
    last_pr_measured_ms = 160.0f;
    next_atrial_time_s = 0.05f;
    next_independent_vent_time_s = 1.0e9f;
    gaussian_model_apply_rhythm(GAUSSIAN_RHYTHM_NORMAL_SINUS);
}

void gaussian_model_set_heart_rate(float bpm) {
    if (bpm < 15.0f) bpm = 15.0f;
    if (bpm > 250.0f) bpm = 250.0f;
    gaussian_model_params.atrial_rate_bpm = bpm;
}

void gaussian_model_reset_defaults(void) {
    set_default_params();
    gaussian_model_apply_rhythm(GAUSSIAN_RHYTHM_NORMAL_SINUS);
}

// ---------------------------------------------------------------------------
// L1: event scheduling and conduction (eq. 1-2)
// ---------------------------------------------------------------------------

static void schedule_next_atrial(float t_atrial) {
    gaussian_model_params_t* p = &gaussian_model_params;
    // Respiratory modulation evaluated at the interval start (eq. 1).
    float mod = 1.0f + p->resp_mod_depth *
                sinf(2.0f * (float)M_PI * p->resp_freq_hz * t_atrial);
    next_atrial_time_s = t_atrial + rr_seconds(p->atrial_rate_bpm) * mod;
}

static bool rhythm_uses_independent_ventricular(void) {
    return (current_rhythm == GAUSSIAN_RHYTHM_COMPLETE_BLOCK) ||
           (current_rhythm == GAUSSIAN_RHYTHM_VENTRICULAR_TACHYCARDIA);
}

static beat_class_t next_ventricular_class(void) {
    return (current_rhythm == GAUSSIAN_RHYTHM_VENTRICULAR_TACHYCARDIA) ?
           BEAT_ECTOPIC : BEAT_NORMAL;
}

static void update_event_schedule(void) {
    gaussian_model_params_t* p = &gaussian_model_params;

    // ---- Atrial events (eq. 1) ----
    // Each atrial event is inserted into the render buffer P_PRE_RENDER_MS
    // before its instant so the P wave ramps up from baseline; the PR / AV-
    // conduction logic (telemetry and the pending ventricular schedule) stays
    // pinned to t_k^A. The buffer's pr_ms/conducted fields are unused by the
    // renderer (only time_s is read), so the pre-render stub values are fine.
    while (true) {
        float t_a = next_atrial_time_s;
        if ((t_a - ms_to_s(P_PRE_RENDER_MS)) > t_sim) break;

        if (t_a > last_rendered_atrial_time_s) {
            push_atrial_event(t_a, 0.0f, false);
            last_rendered_atrial_time_s = t_a;
        }

        if (t_sim < t_a) break;

        schedule_next_atrial(t_a);

        bool conducted = true;
        float pr_ms = p->pr_interval_ms;

        switch (current_rhythm) {
            case GAUSSIAN_RHYTHM_MOBITZ_I:
                // Explicit Wenckebach cycle: conduct while the counter is
                // below the limit, progressively lengthening PR by dPR;
                // when the limit is reached the beat is dropped and both the
                // counter and PR return to their baseline.
                if (mobitz_beats_since_drop >= p->wenckebach_beats) {
                    conducted = false;
                    mobitz_beats_since_drop = 0;
                    mobitz_pr_current_ms = p->pr_interval_ms;
                } else {
                    pr_ms = mobitz_pr_current_ms;
                    mobitz_pr_current_ms += p->pr_increment_ms;
                    mobitz_beats_since_drop++;
                }
                break;
            case GAUSSIAN_RHYTHM_COMPLETE_BLOCK:
            case GAUSSIAN_RHYTHM_VENTRICULAR_TACHYCARDIA:
                conducted = false; // AV dissociation
                break;
            case GAUSSIAN_RHYTHM_ATRIAL_TACHYCARDIA:
                // Simple AV-node refractory protection: block conduction if
                // the previous ventricular event is too recent.
                conducted = (last_ventricular_time_s < 0.0f) ||
                            (s_to_ms(t_a - last_ventricular_time_s) > 280.0f);
                break;
            default:
                break;
        }

        if (conducted) {
            // eq. 2: t_j^V = t_k^A + PR_k
            enqueue_pending_vent(t_a + ms_to_s(pr_ms));
            last_pr_measured_ms = pr_ms;
        }
    }

    // ---- Ventricular events (conducted queue merged with the independent
    //      rhythm schedule of complete block / VT) ----
    bool independent = rhythm_uses_independent_ventricular();
    while (true) {
        float t_v = (pending_vent_count > 0) ? pending_vent_front() : 1.0e9f;
        bool fired_independent = false;
        if (independent && next_independent_vent_time_s <= t_v) {
            t_v = next_independent_vent_time_s;
            fired_independent = true;
        }

        // Rendering starts QRS_PRE_RENDER_MS before the ventricular event so
        // the Q wave and the R upstroke (tau < 0) are drawn; without this the
        // beat began with a vertical cliff at the R peak.
        if ((t_v - ms_to_s(QRS_PRE_RENDER_MS)) > t_sim) break;

        float rr_ms = (last_ventricular_time_s >= 0.0f) ?
                      s_to_ms(t_v - last_ventricular_time_s) : p->rr0_ms;
        beat_class_t cls = fired_independent ? next_ventricular_class() : BEAT_NORMAL;

        // eq. 5: T-wave scaling uses THIS beat's RR interval.
        float fT = powf(rr_ms / p->rr0_ms, p->qt_rate_exponent);
        if (fT < p->qt_scale_min) fT = p->qt_scale_min;
        if (fT > p->qt_scale_max) fT = p->qt_scale_max;

        // Insert into the render buffer exactly once per event (guard by time).
        if (t_v > last_rendered_vent_time_s) {
            push_vent_event(t_v, rr_ms, fT, cls);
            last_rendered_vent_time_s = t_v;
        }

        // Physiology bookkeeping stays pinned to the true ventricular
        // instant: PR/RR telemetry and the PPG pulse foot (eq. 7) use t_v.
        if (t_sim < t_v) break;

        last_ventricular_time_s = t_v;
        last_rr_ms = rr_ms;

        // eq. 7: pulse-foot time; ectopic beats get an extra PEP.
        float pep_ms = p->pep_ms + (cls == BEAT_ECTOPIC ? p->ectopic_pep_ms : 0.0f);
        float amp = (cls == BEAT_ECTOPIC) ? p->ectopic_ppg_amp_scale : 1.0f;
        push_ppg_event(t_v + pat_seconds(pep_ms, p->path_length_m, p->pwv_mps), amp);

        if (fired_independent) {
            next_independent_vent_time_s = t_v + rr_seconds(p->escape_rate_bpm);
        } else {
            dequeue_pending_vent();
        }
    }
}

// ---------------------------------------------------------------------------
// L2: rendering (eq. 3-8) and lead reconstruction (eq. 6)
// ---------------------------------------------------------------------------

void gaussian_model_compute_12_leads(const float* electrodes, float* leads) {
    float r = electrodes[0], l = electrodes[1], f = electrodes[2];
    leads[0] = l - r;                              // I
    leads[1] = f - r;                              // II
    leads[2] = f - l;                              // III
    leads[3] = r - (l + f) * 0.5f;                 // aVR
    leads[4] = l - (r + f) * 0.5f;                 // aVL
    leads[5] = f - (r + l) * 0.5f;                 // aVF
    float wct = (r + l + f) / 3.0f;                // Wilson central terminal
    for (int e = 3; e < GAUSSIAN_MODEL_NUM_LEADS; e++) {
        leads[3 + e] = electrodes[e] - wct;        // V1 ... V6
    }
}

void gaussian_model_compute_output_electrodes(const float* electrodes, float* outputs) {
    // RA is the board reference (its pin carries the PPG), so every ECG pin
    // emits phi_e - phi_RA: GPIO1 = lead I, GPIO2 = lead II and GPIO3..9 =
    // phi_k - phi_RA, which a DUT with grounded RA references to its own
    // Wilson terminal (-phi_RA) and reads back as the exact model Vk.
    float ra = electrodes[0];
    outputs[0] = ra;
    for (int e = 1; e < GAUSSIAN_MODEL_NUM_LEADS; e++) {
        outputs[e] = electrodes[e] - ra;
    }
}

static void update_einthoven_checks(const float* electrodes) {
    // III - (II - I) == 0 and aVR + aVL + aVF == 0 hold exactly for any
    // single-dipole projection; tracking their drift is a guard that the
    // electrode mapping and lead formulas stay consistent.
    float r = electrodes[0], l = electrodes[1], f = electrodes[2];
    float e = fabsf((f - l) - ((f - r) - (l - r)));
    if (e > einthoven_iii_error) einthoven_iii_error = e;

    float w = fabsf((r - (l + f) * 0.5f) + (l - (r + f) * 0.5f) + (f - (r + l) * 0.5f));
    if (w > einthoven_wilson_error) einthoven_wilson_error = w;
}

float gaussian_model_step(float* ecg_out) {
    t_sim += dt;
    update_event_schedule();

    const gaussian_model_params_t* p = &gaussian_model_params;
    float t_now_ms = s_to_ms(t_sim);

    // ---- ECG: cardiac vector projected onto the 9 electrodes ----
    for (int e = 0; e < GAUSSIAN_MODEL_NUM_LEADS; e++) {
        float acc = 0.0f;

        // P waves: rendered from every active atrial event (t_k^A + mu_P).
        // mu_P is clamped to >= 0 to keep the documented P/RR convention.
        // The left bound is P_PRE_RENDER_MS so the P ramps up from baseline.
        float pmu = (p->p_mu_ms >= 0.0f) ? p->p_mu_ms : 0.0f;
        for (uint8_t i = 0; i < num_atrial_events; i++) {
            float tau = t_now_ms - s_to_ms(atrial_events[i].time_s);
            if (tau < -P_PRE_RENDER_MS || tau > ATRIAL_RENDER_WINDOW_MS) continue;
            acc += p->p_amp * DOT_P[e] *
                   gauss_ms(tau, pmu, p->p_sigma_ms, 1.0f);
        }

        // QRS/T: rendered from every active ventricular event (eq. 3).
        for (uint8_t i = 0; i < num_vent_events; i++) {
            const ventricular_event_t* v = &vent_events[i];
            float tau = t_now_ms - s_to_ms(v->time_s);
            if (tau < -VENT_RENDER_WINDOW_MS || tau > VENT_RENDER_WINDOW_MS) continue;

            bool ectopic = (v->cls == BEAT_ECTOPIC);
            float widen = ectopic ? p->ectopic_qrs_widen : 1.0f;
            float shift = ectopic ? p->ectopic_qrs_delay_ms : 0.0f;

            float qG = gauss_ms(tau, p->q_mu_ms - shift, p->q_sigma_ms * widen, 1.0f);
            float rG = gauss_ms(tau, 0.0f,             p->r_sigma_ms * widen, 1.0f);
            float sG = gauss_ms(tau, p->s_mu_ms + shift, p->s_sigma_ms * widen, 1.0f);
            float tG = gauss_ms(tau, p->t_mu_ms * v->t_scale, p->t_sigma_ms * v->t_scale, 1.0f);

            acc += p->q_amp * DOT_Q[e] * qG
                 + p->r_amp * DOT_R[e] * rG
                 + p->s_amp * DOT_S[e] * sG
                 + p->t_amp * DOT_T[e] * tG;
        }

        ecg_out[e] = acc;
    }

    update_einthoven_checks(ecg_out);

    // ---- PPG: overlapping 5-Gaussian pulses with smooth turn-on ----
    float ppg = 0.0f;
    for (uint8_t i = 0; i < num_ppg_events; i++) {
        float tau = t_now_ms - s_to_ms(ppg_events[i].time_s);
        if (tau < LEAD_FIRST_MS || tau > PPG_RENDER_WINDOW_MS) continue;

        // Smooth onset envelope (eq. 8, u(tau)); ~0 before the foot, ~1 a
        // couple of tens of ms after it, no discontinuity.
        float onset = 1.0f / (1.0f + expf(-tau / 8.0f));

        float pulse = 0.0f;
        for (int g = 0; g < 5; g++) {
            pulse += gauss_ms(tau, PPG_TEMPLATE[g].mu_ms,
                              PPG_TEMPLATE[g].sigma_ms, PPG_TEMPLATE[g].a);
        }
        ppg += pulse * onset * ppg_events[i].amplitude;
    }
    return ppg;
}

// ---------------------------------------------------------------------------
// Telemetry getters
// ---------------------------------------------------------------------------

float gaussian_model_get_last_rr_ms(void) { return last_rr_ms; }
float gaussian_model_get_last_pr_ms(void) { return last_pr_measured_ms; }

void gaussian_model_get_einthoven_error(float* err_iii, float* err_wilson) {
    if (err_iii) *err_iii = einthoven_iii_error;
    if (err_wilson) *err_wilson = einthoven_wilson_error;
}

// ---------------------------------------------------------------------------
// Validation (debug only): deterministic 3 s run per rhythm preset
// ---------------------------------------------------------------------------

#if defined(GAUSSIAN_MODEL_SELF_TEST_RUN_AT_BOOT)
#include <stdio.h>
#endif

bool gaussian_model_self_test(void) {
    const float eps = 1.0e-3f;
    int failures = 0;
    float ecg[GAUSSIAN_MODEL_NUM_LEADS];

    // Expected measured PR and RR ranges per preset (paper values):
    //   NSR:           72 bpm, PR 160
    //   1st degree:    72 bpm, PR 240
    //   Mobitz I:      PR cycles 160..280, then a dropped beat
    //   complete block escape 38 bpm (RR ~1579 ms)
    //   atrial tachy:  atrial rate 160 bpm; AV refractory protection turns
    //                  it into a regular 2:1 response (RR ~750 ms)
    //   VT:            160 bpm ectopic (RR ~375 ms)
    typedef struct { float pr_lo, pr_hi, rr_lo, rr_hi; } expect_t;
    static const expect_t EXP[GAUSSIAN_RHYTHM_COUNT] = {
        {160.0f, 160.0f, 817.0f, 850.0f},
        {240.0f, 240.0f, 817.0f, 850.0f},
        {150.0f, 290.0f, 740.0f, 1500.0f},
        {0.0f, 1000.0f, 1545.0f, 1612.0f},
        {160.0f, 160.0f, 705.0f, 795.0f},
        {0.0f, 1000.0f, 355.0f, 395.0f},
    };

    gaussian_model_init(2000.0f);

    for (uint8_t r = 0; r < GAUSSIAN_RHYTHM_COUNT; r++) {
        gaussian_model_apply_rhythm(r);
        for (int i = 0; i < 6000; i++) {   // 3 s at 2000 Hz
            (void)gaussian_model_step(ecg);
        }

        float err_iii = 0.0f, err_w = 0.0f;
        gaussian_model_get_einthoven_error(&err_iii, &err_w);
        float pr = gaussian_model_get_last_pr_ms();
        float rr = gaussian_model_get_last_rr_ms();

        // Hardware output-mapping check: the RA-referenced pin values must be
        // read back by a DUT with grounded RA as the model's 12 leads
        // (gaussian_model_compute_output_electrodes).
        float out_pin[GAUSSIAN_MODEL_NUM_LEADS];
        float leads_model[GAUSSIAN_MODEL_NUM_LEADS_12];
        gaussian_model_compute_output_electrodes(ecg, out_pin);
        gaussian_model_compute_12_leads(ecg, leads_model);
        const float i_pin = out_pin[1], ii_pin = out_pin[2];
        float leads_dut[GAUSSIAN_MODEL_NUM_LEADS_12];
        leads_dut[0] = i_pin;                          // I
        leads_dut[1] = ii_pin;                         // II
        leads_dut[2] = ii_pin - i_pin;                 // III
        leads_dut[3] = -(i_pin + ii_pin) * 0.5f;       // aVR
        leads_dut[4] = i_pin - ii_pin * 0.5f;          // aVL
        leads_dut[5] = ii_pin - i_pin * 0.5f;          // aVF
        float wct_dut = (i_pin + ii_pin) / 3.0f;       // (0 + I + II)/3
        for (int e = 3; e < GAUSSIAN_MODEL_NUM_LEADS; e++) {
            leads_dut[3 + e] = out_pin[e] - wct_dut;   // V1 ... V6
        }
        float out_err = 0.0f;
        for (int i = 0; i < GAUSSIAN_MODEL_NUM_LEADS_12; i++) {
            float d = fabsf(leads_dut[i] - leads_model[i]);
            if (d > out_err) out_err = d;
        }

        bool pr_ok = (pr >= EXP[r].pr_lo && pr <= EXP[r].pr_hi);
        bool rr_ok = (rr >= EXP[r].rr_lo && rr <= EXP[r].rr_hi);
        bool eh_ok = (err_iii < eps && err_w < eps);
        bool out_ok = (out_err < eps);
        if (!(pr_ok && rr_ok && eh_ok && out_ok)) failures++;

#if defined(GAUSSIAN_MODEL_SELF_TEST_RUN_AT_BOOT)
        printf("SELFTEST r=%u %-24s PR=%.1f RR=%.1f eIII=%.2e eWilson=%.2e %s%s%s%s\r\n",
               r, rhythm_names[r], (double)pr, (double)rr,
               (double)err_iii, (double)err_w,
               pr_ok ? "PR-ok " : "PR-FAIL ",
               rr_ok ? "RR-ok " : "RR-FAIL ",
               eh_ok ? "EIN-ok " : "EIN-FAIL ",
               out_ok ? "OUT-ok" : "OUT-FAIL");
#endif
    }

    gaussian_model_apply_rhythm(GAUSSIAN_RHYTHM_NORMAL_SINUS);

#if defined(GAUSSIAN_MODEL_SELF_TEST_RUN_AT_BOOT)
    printf("SELFTEST %s (%d failures)\r\n", failures ? "FAILED" : "PASSED", failures);
#endif
    return failures == 0;
}