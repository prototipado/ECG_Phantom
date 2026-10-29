// Implementation of the extended heterogeneous oscillator model of the cardiac
// conduction system described in:
//   M. A. Quiroz-Juarez, O. Jimenez-Ramirez, R. Vazquez-Medina, E. Ryzhii,
//   M. Ryzhii and J. L. Aragon, "Cardiac Conduction Model for Generating
//   12 Lead ECG Signals With Realistic Heart Rate Dynamics," IEEE Trans.
//   Nanobioscience, vol. 17, no. 4, pp. 525-532, Oct. 2018.
//
// Natural pacemakers (SA, AV, HP) are modified Van der Pol oscillators with
// delayed unidirectional coupling (eq. 1). Atrial/ventricular muscle response
// (P wave, Ta wave, QRS complex, T wave) are modified FitzHugh-Nagumo
// oscillators driven by the pacemakers (eq. 2). The single-lead ECG is a
// weighted sum of the four muscle responses (eq. 3/6). Because the exact
// per-lead weighting table (Table I in the paper) is a rendered table not
// recoverable from the source PDF text layer, this module uses a documented
// heuristic relative-gain table (see LEAD_GAIN below) tuned to reproduce the
// qualitative morphology of each lead (R-wave progression V1-V6, etc.). All
// physiologically meaningful coefficients remain editable at runtime.
#include "cardiac_model.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cardiac_model_params_t cardiac_model_params;

static float dt = 1.0f / 2000.0f; // integration step (s)
static float t_sim = 0.0f;
static uint8_t current_rhythm = CARDIAC_RHYTHM_NORMAL;

// ---- State vector ----
// Pacemakers: x1,y1 (SA), x2,y2 (AV), x3,y3 (HP)
// Muscles: z1,v1 (P), z2,v2 (Ta), z3,v3 (QRS), z4,v4 (T)
typedef struct {
    float x1, y1, x2, y2, x3, y3;
    float z1, v1, z2, v2, z3, v3, z4, v4;
} model_state_t;

static model_state_t st;

// f1 is the SA node intrinsic frequency parameter; it is derived from the
// target heart rate via the binomial fit of eq. (7) and also serves as the
// dynamic coupling gain K_SA-AV = K_AV-HP = f1.
static float f1_base = 22.0f;
static float f1_now = 22.0f;

// Delay lines for y1 (SA->AV) and y2 (AV->HP), sized for the maximum
// supported delay at the fixed integration rate.
#define DELAY_BUF_LEN 256
static float y1_hist[DELAY_BUF_LEN];
static float y2_hist[DELAY_BUF_LEN];
static uint16_t hist_idx = 0;

// Per-lead relative gain heuristic: {alpha1_scale, alpha2_scale, alpha3_scale, alpha4_scale}
// Order: LA(=I), LL(=II), V1, V2, V3, V4, V5, V6
static const float LEAD_GAIN[8][4] = {
    /* I   */ {0.55f, 0.50f, 0.75f, 0.55f},
    /* II  */ {1.00f, 0.70f, 1.00f, 1.00f},
    /* V1  */ {0.30f, 0.40f, -0.45f, 0.30f},
    /* V2  */ {0.40f, 0.40f, 0.60f, 0.50f},
    /* V3  */ {0.50f, 0.40f, 1.00f, 0.70f},
    /* V4  */ {0.60f, 0.40f, 1.20f, 0.80f},
    /* V5  */ {0.60f, 0.40f, 1.00f, 0.90f},
    /* V6  */ {0.60f, 0.40f, 0.80f, 0.90f},
};

static void set_default_params(void) {
    cardiac_model_params.a1 = 40.0f;
    cardiac_model_params.a2 = 50.0f;
    cardiac_model_params.a3 = 50.0f;
    cardiac_model_params.f2 = 8.4f;
    cardiac_model_params.f3 = 1.5f;
    cardiac_model_params.u  = 0.69f;
    cardiac_model_params.d  = 3.0f;
    cardiac_model_params.e1 = 3.5f;
    cardiac_model_params.e2 = 5.0f;
    cardiac_model_params.e3 = 12.0f;
    cardiac_model_params.tau_node_ms = 4.0f;

    cardiac_model_params.alpha1 = 1.0f;
    cardiac_model_params.alpha2 = 1.0f;
    cardiac_model_params.alpha3 = 1.0f;
    cardiac_model_params.alpha4 = 1.0f;

    cardiac_model_params.hrv_depth = 0.05f;
    cardiac_model_params.baseline_z0_mV = 0.15f;
    cardiac_model_params.baseline_f0_hz = 0.1f;
    cardiac_model_params.hrv_enabled = true;
    cardiac_model_params.baseline_wander_enabled = true;
}

static const char* rhythm_names[CARDIAC_RHYTHM_COUNT] = {
    "Normal Sinus",
    "Sinus Bradycardia",
    "Sinus Tachycardia",
    "SA-AV Block",
    "AV-HP Block",
    "Wellens V1-V3 Ischemia",
};

const char* cardiac_model_get_rhythm_name(uint8_t index) {
    if (index >= CARDIAC_RHYTHM_COUNT) return "?";
    return rhythm_names[index];
}

uint8_t cardiac_model_get_rhythm_count(void) { return CARDIAC_RHYTHM_COUNT; }
uint8_t cardiac_model_get_current_rhythm(void) { return current_rhythm; }

// k_sa_av / k_av_hp are dynamically equal to f1 (eq. after Fig.2 in the paper);
// block rhythms are emulated by forcing these gains toward zero.
static float k_sa_av_scale = 1.0f;
static float k_av_hp_scale = 1.0f;

void cardiac_model_apply_rhythm(uint8_t index) {
    if (index >= CARDIAC_RHYTHM_COUNT) return;
    current_rhythm = index;

    // Reset weighting to normal defaults before applying the preset deltas.
    cardiac_model_params.alpha1 = 1.0f;
    cardiac_model_params.alpha2 = 1.0f;
    cardiac_model_params.alpha3 = 1.0f;
    cardiac_model_params.alpha4 = 1.0f;
    k_sa_av_scale = 1.0f;
    k_av_hp_scale = 1.0f;

    switch (index) {
        case CARDIAC_RHYTHM_NORMAL:
            break;
        case CARDIAC_RHYTHM_SINUS_BRADYCARDIA:
            // Handled by setting heart rate <60 bpm from the menu; no extra change.
            break;
        case CARDIAC_RHYTHM_SINUS_TACHYCARDIA:
            // Handled by setting heart rate >100 bpm from the menu; no extra change.
            break;
        case CARDIAC_RHYTHM_SA_AV_BLOCK:
            // SA->AV coupling suppressed: AV node desynchronizes from SA node.
            k_sa_av_scale = 0.0f;
            break;
        case CARDIAC_RHYTHM_AV_HP_BLOCK:
            // AV->HP coupling suppressed: ventricles desynchronize from AV node.
            k_av_hp_scale = 0.0f;
            break;
        case CARDIAC_RHYTHM_WELLENS_V1V3:
            // Approximate ST-elevation / T-wave inversion in precordial leads by
            // inverting/boosting the T-wave weight; applied uniformly here as a
            // simplification (paper applies a separate weighting table per lead).
            cardiac_model_params.alpha4 = -1.4f;
            cardiac_model_params.alpha3 = 1.2f;
            break;
    }
}

void cardiac_model_init(float sample_rate_hz) {
    if (sample_rate_hz > 0) dt = 1.0f / sample_rate_hz;
    set_default_params();
    memset(&st, 0, sizeof(st));
    // Initial conditions from the paper: xi = -0.1, yi = 0.025, zi = 0, vi = 0
    st.x1 = st.x2 = st.x3 = -0.1f;
    st.y1 = st.y2 = st.y3 = 0.025f;
    memset(y1_hist, 0, sizeof(y1_hist));
    memset(y2_hist, 0, sizeof(y2_hist));
    hist_idx = 0;
    t_sim = 0.0f;
    current_rhythm = CARDIAC_RHYTHM_NORMAL;
    cardiac_model_set_heart_rate(72.0f);
}

void cardiac_model_set_heart_rate(float bpm) {
    if (bpm < 15.0f) bpm = 15.0f;
    if (bpm > 250.0f) bpm = 250.0f;
    float f = bpm / 60.0f; // Hz
    // Binomial fit from eq. (7): f1 = -0.7319 + 8.798*f + 9.098*f^2
    f1_base = -0.7319f + 8.798f * f + 9.098f * f * f;
    if (f1_base < 1.0f) f1_base = 1.0f;
}

static inline float heaviside(float y) { return (y > 0.0f) ? 1.0f : 0.0f; }

// Derivative evaluation for RK4. `hist_y1`/`hist_y2` are the delayed samples.
static void deriv(const model_state_t* s, float delayed_y1, float delayed_y2,
                   model_state_t* d) {
    const cardiac_model_params_t* p = &cardiac_model_params;

    // ---- Pacemakers (Van der Pol), eq. (1) ----
    // F(x) = a*(x^2 - u), G(x) = f*x*(x+d)*(x+e)
    float Fx1 = p->a1 * (s->x1 * s->x1 - p->u);
    float Gx1 = f1_now * s->x1 * (s->x1 + p->d) * (s->x1 + p->e1);
    d->x1 = s->y1;
    d->y1 = -Fx1 * s->y1 - Gx1; // K_AV-SA = 0 (unidirectional chain)

    float Fx2 = p->a2 * (s->x2 * s->x2 - p->u);
    float Gx2 = p->f2 * s->x2 * (s->x2 + p->d) * (s->x2 + p->e2);
    float k_sa_av = f1_now * k_sa_av_scale;
    d->x2 = s->y2;
    d->y2 = -Fx2 * s->y2 - Gx2 + k_sa_av * (delayed_y1 - s->y2);

    float Fx3 = p->a3 * (s->x3 * s->x3 - p->u);
    float Gx3 = p->f3 * s->x3 * (s->x3 + p->d) * (s->x3 + p->e3);
    float k_av_hp = f1_now * k_av_hp_scale;
    d->x3 = s->y3;
    d->y3 = -Fx3 * s->y3 - Gx3 + k_av_hp * (delayed_y2 - s->y3);

    // ---- Muscles (FitzHugh-Nagumo), eq. (2) ----
    // Fixed physiological parameters (see header comment for source values).
    static const float k[4] = {2.0e3f, 1.0e2f, 1.0e4f, 2.0e3f};
    static const float c[4] = {0.26f, 0.12f, 0.12f, 0.1f};
    static const float b[4] = {0.0f, 0.0f, 0.015f, 0.0f};
    static const float g[4] = {0.4f, 0.09f, 0.09f, 0.1f};
    static const float h[4] = {0.004f, 0.008f, 0.008f, 0.008f};
    static const float w1[4] = {0.13f, 0.12f, 0.12f, 0.22f};
    static const float w2[4] = {1.0f, 1.1f, 1.1f, 0.8f};
    // Correction: the paper's text publishes C4 = -6e-5, but that sign makes
    // z4 (T-wave oscillator) negative, yielding inverted T in II/aVF/V2-V6
    // and upright T in aVR — anti-physiological.  The paper's Table I (alpha4
    // positive in II/aVF/V2-V6, negative in aVR) is only consistent with
    // z4 > 0.  Using C4 = +6e-5 (keeping Y4 = -y3 so the T fires after the
    // QRS) reproduces the physiological T polarity.  See the Python diagnosis
    // and the HTML ecg_model_sim.html for verification.
    static const float C[4] = {4.0e-5f, -4.0e-5f, 9.0e-5f, 6.0e-5f};

    float Y1 = s->y1;
    float Y2 = -s->y1;
    float Y3 = s->y3;
    float Y4 = -s->y3;
    float Y[4] = {Y1, Y2, Y3, Y4};
    float z[4] = {s->z1, s->z2, s->z3, s->z4};
    float v[4] = {s->v1, s->v2, s->v3, s->v4};
    float dz[4], dv[4];

    for (int j = 0; j < 4; j++) {
        float I = C[j] * Y[j] * heaviside(Y[j]);
        dz[j] = k[j] * (-c[j] * z[j] * (z[j] - w1[j]) * (z[j] - w2[j])
                         - b[j] * v[j] - g[j] * v[j] * z[j] + I);
        dv[j] = k[j] * h[j] * (z[j] - v[j]);
    }
    d->z1 = dz[0]; d->v1 = dv[0];
    d->z2 = dz[1]; d->v2 = dv[1];
    d->z3 = dz[2]; d->v3 = dv[2];
    d->z4 = dz[3]; d->v4 = dv[3];
}

static void add_scaled(model_state_t* out, const model_state_t* a, const model_state_t* b, float s) {
    const float* ba = (const float*)a;
    const float* bb = (const float*)b;
    float* bo = (float*)out;
    int n = sizeof(model_state_t) / sizeof(float);
    for (int i = 0; i < n; i++) bo[i] = ba[i] + s * bb[i];
}

void cardiac_model_step(float* out) {
    // Update dynamic SA frequency / coupling gain with optional HRV modulation
    // approximating the RSA (~0.25 Hz) and Mayer (~0.1 Hz) waves of eq. (4).
    t_sim += dt;
    if (cardiac_model_params.hrv_enabled) {
        float rsa = sinf(2.0f * (float)M_PI * 0.25f * t_sim);
        float mayer = sinf(2.0f * (float)M_PI * 0.1f * t_sim);
        f1_now = f1_base * (1.0f + cardiac_model_params.hrv_depth * (0.6f * rsa + 0.4f * mayer));
    } else {
        f1_now = f1_base;
    }
    if (f1_now < 0.5f) f1_now = 0.5f;

    // Fetch delayed samples for coupling.
    uint16_t delay_samples = (uint16_t)(cardiac_model_params.tau_node_ms * 0.001f / dt);
    if (delay_samples >= DELAY_BUF_LEN) delay_samples = DELAY_BUF_LEN - 1;
    uint16_t read_idx = (hist_idx + DELAY_BUF_LEN - delay_samples) % DELAY_BUF_LEN;
    float delayed_y1 = y1_hist[read_idx];
    float delayed_y2 = y2_hist[read_idx];

    // RK4 integration of the full state vector.
    model_state_t k1, k2, k3, k4, tmp;
    deriv(&st, delayed_y1, delayed_y2, &k1);
    add_scaled(&tmp, &st, &k1, dt * 0.5f);
    deriv(&tmp, delayed_y1, delayed_y2, &k2);
    add_scaled(&tmp, &st, &k2, dt * 0.5f);
    deriv(&tmp, delayed_y1, delayed_y2, &k3);
    add_scaled(&tmp, &st, &k3, dt);
    deriv(&tmp, delayed_y1, delayed_y2, &k4);

    float* s = (float*)&st;
    const float* a1 = (const float*)&k1;
    const float* a2 = (const float*)&k2;
    const float* a3 = (const float*)&k3;
    const float* a4 = (const float*)&k4;
    int n = sizeof(model_state_t) / sizeof(float);
    for (int i = 0; i < n; i++) {
        s[i] += (dt / 6.0f) * (a1[i] + 2.0f * a2[i] + 2.0f * a3[i] + a4[i]);
    }

    // Push current y1/y2 into the delay history for future samples.
    y1_hist[hist_idx] = st.y1;
    y2_hist[hist_idx] = st.y2;
    hist_idx = (hist_idx + 1) % DELAY_BUF_LEN;

    // Baseline wander term zn(t), eq. (9).
    float baseline = 0.0f;
    if (cardiac_model_params.baseline_wander_enabled) {
        baseline = cardiac_model_params.baseline_z0_mV *
                   sinf(2.0f * (float)M_PI * cardiac_model_params.baseline_f0_hz * t_sim);
    }

    const cardiac_model_params_t* p = &cardiac_model_params;
    out[0] = 0.0f; // RA reference electrode
    for (int lead = 0; lead < 8; lead++) {
        float a1w = LEAD_GAIN[lead][0] * p->alpha1;
        float a2w = LEAD_GAIN[lead][1] * p->alpha2;
        float a3w = LEAD_GAIN[lead][2] * p->alpha3;
        float a4w = LEAD_GAIN[lead][3] * p->alpha4;
        float ecg = baseline + a1w * st.z1 - a2w * st.z2 + a3w * st.z3 + a4w * st.z4;
        out[1 + lead] = ecg;
    }
}
