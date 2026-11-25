#include "signal_gen.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include <stdio.h>
#include "interface.h"
#include "gaussian_ecg_ppg_model.h"
#include "streaming.h"

stream_buffer_t stream_buffer;
volatile bool stream_mode_active = false;

// Pins: 0, 1, 2, 3, 4, 6, 7, 8, 9  ->  RA*(PPG), LA(I), LL(II), V1..V6
//
// *RA (GPIO0) carries the PPG waveform instead of the RA electrode voltage.
//  In the Einthoven triangle the RA node is the circuit reference, so the RA
//  potential is not needed on a pin: the device under test ties its RA input
//  to GND. GPIO0 is mapped to the calibrated safe output interval to drive an
//  IR/red LED for a pulse-oximeter sensor head. The RA ECG electrode must
//  therefore be connected to GND on the PCB.
//
//  Because the RA potential is not emitted, the remaining ECG pins carry the
//  RA-referenced electrode potentials (gaussian_model_compute_output_electrodes):
//    GPIO1 = phi_LA - phi_RA = lead I
//    GPIO2 = phi_LL - phi_RA = lead II
//    GPIO3..9 = phi_k - phi_RA
//  A DUT with its RA input grounded rebuilds its Wilson terminal as
//  (0 + I + II)/3 = -phi_RA and therefore measures exactly the model's 12
//  leads (I, II, III, aVR, aVL, aVF, V1..V6).
//
// Pin 10 is an ADDED hardware channel (not present in the base pico_ecg
// project) carrying the same PPG waveform for monitoring / logging.
static const uint signal_pins[] = {0, 1, 2, 3, 4, 6, 7, 8, 9};
#define NUM_SIGNAL_PINS (sizeof(signal_pins) / sizeof(signal_pins[0]))
#define PPG_PIN 10
// Index of the RA pin inside signal_pins[] (re-purposed as PPG output).
#define RA_PIN_INDEX 0
// Pre-calibration fallback ceiling for the PPG output (0..65535 scale).
// After signal_gen_calibrate_startup() succeeds this is replaced by the
// ADC-measured value that corresponds to STARTUP_CAL_SAFE_ADC_MAX (~2.4 V).
// Chosen conservatively so the op-amp is not over-driven before calibration.
#define PPG_RA_SCALE 50000.0f
#define ECG_SAMPLE_RATE_HZ 2000.0f
#define STARTUP_CAL_POINTS 256
#define STARTUP_CAL_SETTLE_POINTS 64
#define STARTUP_CAL_AMPLITUDE 10000
#define STARTUP_CAL_MODEL_WARMUP_SAMPLES (2 * (int)ECG_SAMPLE_RATE_HZ)
#define STARTUP_CAL_MODEL_SAMPLES (8 * (int)ECG_SAMPLE_RATE_HZ)
#define STARTUP_CAL_SAFE_ADC_MIN 300
#define STARTUP_CAL_SAFE_ADC_MAX 3100
#define OUTPUT_DIVIDER_TOP_OHMS 100000.0f
#define OUTPUT_DIVIDER_BOTTOM_OHMS 1000.0f

static volatile bool is_running = true;
static volatile signal_type_t current_signal_type = SIGNAL_TYPE_ECG;
static volatile uint16_t signal_amplitude = 8000;
static volatile uint16_t signal_bpm = 72;
static volatile int16_t signal_offset = 0;
static volatile float signal_cal_gain = 1.0f;  // Output calibration multiplier
static volatile int16_t ecg_calibration_offset = 0;
static volatile uint16_t ecg_output_min = 0;
static volatile uint16_t ecg_output_max = 65535;
static volatile float calibration_adc_mean = 2047.5f;
static volatile float calibration_adc_counts_per_pwm_count = 0.0f;
static volatile bool startup_calibration_valid = false;

// PPG output scale (0-1). Allows reducing PPG peak to avoid oscilloscope clipping.
static volatile float ppg_out_scale = 0.80f;
// PWM bounds corresponding to the calibrated safe ADC range. The PPG is
// unipolar, so it must be mapped between both bounds rather than from zero.
static volatile uint16_t ppg_ra_pwm_ceil = (uint16_t)PPG_RA_SCALE;
static volatile uint16_t ppg_ra_pwm_floor = 0;

// PPG pre-emphasis removed

static const int16_t sine_lut[256] = {
    0, 402, 804, 1205, 1606, 2005, 2404, 2801, 3196, 3589, 3980, 4369, 4756, 5139, 5519, 5896,
    6270, 6639, 7005, 7366, 7723, 8075, 8423, 8765, 9102, 9434, 9759, 10079, 10393, 10701, 11002, 11297,
    11585, 11866, 12139, 12406, 12665, 12916, 13159, 13395, 13622, 13842, 14053, 14255, 14449, 14634, 14810, 14978,
    15136, 15286, 15426, 15557, 15678, 15790, 15893, 15985, 16069, 16142, 16206, 16260, 16305, 16339, 16364, 16380,
    16384, 16380, 16364, 16339, 16305, 16260, 16206, 16142, 16069, 15985, 15893, 15790, 15678, 15557, 15426, 15286,
    15136, 14978, 14810, 14634, 14449, 14255, 14053, 13842, 13622, 13395, 13159, 12916, 12665, 12406, 12139, 11866,
    11585, 11297, 11002, 10701, 10393, 10079, 9759, 9434, 9102, 8765, 8423, 8075, 7723, 7366, 7005, 6639,
    6270, 5896, 5519, 5139, 4756, 4369, 3980, 3589, 3196, 2801, 2404, 2005, 1606, 1205, 804, 402,
    0, -402, -804, -1205, -1606, -2005, -2404, -2801, -3196, -3589, -3980, -4369, -4756, -5139, -5519, -5896,
    -6270, -6639, -7005, -7366, -7723, -8075, -8423, -8765, -9102, -9434, -9759, -10079, -10393, -10701, -11002, -11297,
    -11585, -11866, -12139, -12406, -12665, -12916, -13159, -13395, -13622, -13842, -14053, -14255, -14449, -14634, -14810, -14978,
    -15136, -15286, -15426, -15557, -15678, -15790, -15893, -15985, -16069, -16142, -16206, -16260, -16305, -16339, -16364, -16380,
    -16384, -16380, -16364, -16339, -16305, -16260, -16206, -16142, -16069, -15985, -15893, -15790, -15678, -15557, -15426, -15286,
    -15136, -14978, -14810, -14634, -14449, -14255, -14053, -13842, -13622, -13395, -13159, -12916, -12665, -12406, -12139, -11866,
    -11585, -11297, -11002, -10701, -10393, -10079, -9759, -9434, -9102, -8765, -8423, -8075, -7723, -7366, -7005, -6639,
    -6270, -5896, -5519, -5139, -4756, -4369, -3980, -3589, -3196, -2801, -2404, -2005, -1606, -1205, -804, -402
};

static void configure_pwm_pin(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    uint32_t clock_rate = clock_get_hz(clk_sys);
    uint32_t wrap = clock_rate / 100000;
    if (wrap < 1) wrap = 1;
    pwm_set_wrap(slice_num, wrap - 1);
    pwm_set_clkdiv(slice_num, 1.0f);
    pwm_set_enabled(slice_num, true);
}

void signal_gen_init(void) {
    for (int i = 0; i < NUM_SIGNAL_PINS; i++) configure_pwm_pin(signal_pins[i]);
    configure_pwm_pin(PPG_PIN);
    gaussian_model_init(ECG_SAMPLE_RATE_HZ);
}

void signal_gen_start(void) { is_running = true; }
void signal_gen_stop(void) { is_running = false; }
void signal_gen_set_signal_type(signal_type_t type) { current_signal_type = type; }

void signal_gen_set_amplitude(uint16_t amplitude) {
    if (amplitude > 32768) amplitude = 32768;
    signal_amplitude = amplitude;
}

void signal_gen_set_frequency(uint16_t frequency) { signal_gen_set_bpm(frequency * 60); }

void signal_gen_set_bpm(uint16_t bpm) {
    if (bpm < 1) bpm = 1;
    if (bpm > 3000) bpm = 3000;
    signal_bpm = bpm;
    gaussian_model_set_heart_rate((float)bpm);
}

void signal_gen_set_offset(int16_t offset) {
    if (offset < -16384) offset = -16384;
    if (offset > 16384) offset = 16384;
    signal_offset = offset;
}

bool signal_gen_calibrate_startup(void) {
    // Calibrate the actual ECG output path (GPIO1 = lead I). GPIO0 is the
    // unipolar PPG output and is not representative of the ECG pin mapping.
    const uint pin = signal_pins[1];
    const uint slice_num = pwm_gpio_to_slice_num(pin);
    const uint channel = pwm_gpio_to_channel(pin);
    const uint32_t wrap = clock_get_hz(clk_sys) / 100000;
    int32_t minimum = 4095;
    int32_t maximum = 0;
    uint32_t sum = 0;
    uint32_t sample_count = 0;
    bool adc_clipped = false;
    float model_min = 3.402823466e+38F;
    float model_max = -3.402823466e+38F;
    float model_out[GAUSSIAN_MODEL_NUM_LEADS];
    startup_calibration_valid = false;
    ecg_calibration_offset = 0;

    printf("Startup calibration: lead-I sine on GPIO1, read ADC0/GPIO26...\r\n");
    fflush(stdout);

    for (int i = 0; i < STARTUP_CAL_POINTS; i++) {
        uint8_t phase = (uint8_t)((i % (STARTUP_CAL_POINTS / 4)) * 4);
        int32_t test_value = 32768
            + ((int32_t)sine_lut[phase] * STARTUP_CAL_AMPLITUDE) / 16384;
        uint32_t level = ((uint32_t)test_value * (wrap - 1)) / 65535;
        pwm_set_chan_level(slice_num, channel, (uint16_t)level);
        sleep_ms(2);

        uint16_t raw = interface_read_calibration_adc();
        if (i < STARTUP_CAL_SETTLE_POINTS) continue;
        if (raw <= 3 || raw >= 4092) adc_clipped = true;
        if (raw < minimum) minimum = raw;
        if (raw > maximum) maximum = raw;
        sum += raw;
        sample_count++;
    }
    pwm_set_chan_level(slice_num, channel, 0);

    float measured_amplitude = (float)(maximum - minimum) * 0.5f;
    float measured_mean = (float)sum / (float)sample_count;
    float counts_per_pwm_count =
        measured_amplitude / (float)STARTUP_CAL_AMPLITUDE;
    // Must match the runtime ECG conversion below exactly. The calibrated
    // gain is applied to this same base scale during normal generation.
    float base_counts_per_mV = (float)signal_amplitude * 2.5f;

    gaussian_model_init(ECG_SAMPLE_RATE_HZ);
    gaussian_model_set_heart_rate((float)signal_bpm);
    for (int i = 0; i < STARTUP_CAL_MODEL_WARMUP_SAMPLES
         + STARTUP_CAL_MODEL_SAMPLES; i++) {
        float out_pin[GAUSSIAN_MODEL_NUM_LEADS];
        gaussian_model_step(model_out);
        if (i < STARTUP_CAL_MODEL_WARMUP_SAMPLES) continue;
        // The pins carry the RA-referenced potentials (channel 0, the RA
        // reference, is not driven: GPIO0 is the PPG output), so the probed
        // range must use exactly the values that will reach the PWM pins.
        gaussian_model_compute_output_electrodes(model_out, out_pin);
        for (int col = 1; col < GAUSSIAN_MODEL_NUM_LEADS; col++) {
            float unscaled_pwm = out_pin[col] * base_counts_per_mV;
            if (unscaled_pwm < model_min) model_min = unscaled_pwm;
            if (unscaled_pwm > model_max) model_max = unscaled_pwm;
        }
    }

    float target_adc_mid =
        ((float)STARTUP_CAL_SAFE_ADC_MIN + STARTUP_CAL_SAFE_ADC_MAX) * 0.5f;
    float gain = ((float)STARTUP_CAL_SAFE_ADC_MAX
                  - STARTUP_CAL_SAFE_ADC_MIN)
        / (counts_per_pwm_count * (model_max - model_min));
    float offset = ((target_adc_mid - measured_mean) / counts_per_pwm_count)
        - ((model_min + model_max) * 0.5f) * gain;
    float safe_min = 32768.0f
        + ((float)STARTUP_CAL_SAFE_ADC_MIN - measured_mean)
            / counts_per_pwm_count;
    float safe_max = 32768.0f
        + ((float)STARTUP_CAL_SAFE_ADC_MAX - measured_mean)
            / counts_per_pwm_count;

    if (adc_clipped || measured_amplitude < 8.0f ||
        measured_mean < 100.0f || measured_mean > 3995.0f ||
        !(model_min < model_max) ||
        !(gain >= 0.01f && gain <= 100.0f) ||
        !(offset >= -16384.0f && offset <= 16384.0f) ||
        !(safe_min >= 0.0f && safe_min < safe_max && safe_max <= 65535.0f)) {
        printf("Startup calibration failed; keeping ECG calibration offset=0, cal=1.000.\r\n");
        fflush(stdout);
        gaussian_model_init(ECG_SAMPLE_RATE_HZ);
        gaussian_model_set_heart_rate((float)signal_bpm);
        return false;
    }

    ecg_calibration_offset =
        (int16_t)(offset >= 0.0f ? offset + 0.5f : offset - 0.5f);
    signal_cal_gain = gain;
    ecg_output_min = (uint16_t)(safe_min + 0.5f);
    ecg_output_max = (uint16_t)(safe_max + 0.5f);
    calibration_adc_mean = measured_mean;
    calibration_adc_counts_per_pwm_count = counts_per_pwm_count;
    startup_calibration_valid = true;

    // PPG is unipolar. Map zero to the safe lower output and its peak to 80%
    // of the calibrated safe span; mapping from PWM zero would drive the
    // output below the measured safe range.
    ppg_ra_pwm_floor = (uint16_t)(safe_min + 0.5f);
    ppg_ra_pwm_ceil = (uint16_t)(safe_max + 0.5f);

    unsigned int gain_milli = (unsigned int)(gain * 1000.0f + 0.5f);
    printf("Startup calibration OK: offset=%d, cal=%u.%03u, ADC target=%u..%u\r\n",
           ecg_calibration_offset, gain_milli / 1000, gain_milli % 1000,
           STARTUP_CAL_SAFE_ADC_MIN, STARTUP_CAL_SAFE_ADC_MAX);
    printf("Model range at nominal amplitude: %.2f..%.2f PWM counts\r\n",
           (double)model_min, (double)model_max);
    printf("PPG safe PWM range: %u..%u counts (ADC target %u..%u)\r\n",
           (unsigned)ppg_ra_pwm_floor, (unsigned)ppg_ra_pwm_ceil,
           STARTUP_CAL_SAFE_ADC_MIN, STARTUP_CAL_SAFE_ADC_MAX);
    fflush(stdout);

    gaussian_model_init(ECG_SAMPLE_RATE_HZ);
    gaussian_model_set_heart_rate((float)signal_bpm);
    return true;
}

static float pwm_value_to_stream_mv(int32_t pwm_value) {
    float adc_value = calibration_adc_mean
        + ((float)pwm_value - 32768.0f)
            * calibration_adc_counts_per_pwm_count;
    if (adc_value < 0.0f) adc_value = 0.0f;
    if (adc_value > 4095.0f) adc_value = 4095.0f;

    float voltage_at_filter_mv = (adc_value - calibration_adc_mean)
        * (3300.0f / 4095.0f);
    float divider_ratio = OUTPUT_DIVIDER_BOTTOM_OHMS
        / (OUTPUT_DIVIDER_TOP_OHMS + OUTPUT_DIVIDER_BOTTOM_OHMS);
    return voltage_at_filter_mv * divider_ratio;
}

void signal_gen_set_cal_gain(float gain) {
    if (gain < 0.01f) gain = 0.01f;
    if (gain > 100.0f) gain = 100.0f;
    signal_cal_gain = gain;
}

float signal_gen_get_cal_gain(void) {
    return signal_cal_gain;
}

void signal_gen_set_ppg_scale(float scale) {
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;
    ppg_out_scale = scale;
}
float signal_gen_get_ppg_scale(void) {
    return ppg_out_scale;
}

signal_type_t signal_gen_get_signal_type(void) { return current_signal_type; }
uint16_t signal_gen_get_bpm(void) { return signal_bpm; }

static int32_t generate_sine(uint16_t phase) {
    int32_t center = 32768;
    int32_t amplitude = (int32_t)signal_amplitude;
    uint8_t idx = (uint8_t)(phase >> 8);
    int32_t lut_val = (int32_t)sine_lut[idx];
    int32_t scaled_sine = (lut_val * amplitude) / 16384;
    return center + scaled_sine;
}

static int32_t generate_triangle(uint16_t phase) {
    int32_t center = 32768;
    int32_t amplitude = (int32_t)signal_amplitude;
    int32_t tri_val = 0;
    if (phase < 16384) {
        tri_val = ((int32_t)phase * amplitude) / 16384;
    } else if (phase < 49152) {
        tri_val = -(((int32_t)phase - 32768) * amplitude) / 16384;
    } else {
        tri_val = (((int32_t)phase - 65536) * amplitude) / 16384;
    }
    return center + tri_val;
}

static int32_t generate_square(uint16_t phase) {
    int32_t center = 32768;
    int32_t amplitude = (int32_t)signal_amplitude;
    return (phase < 32768) ? (center + amplitude) : (center - amplitude);
}

static int32_t generate_pulse(uint16_t phase) {
    int32_t center = 32768;
    int32_t amplitude = (int32_t)signal_amplitude;
    const uint16_t pulse_threshold = 3277; // 5% duty cycle
    return (phase < pulse_threshold) ? (center + amplitude) : (center - amplitude);
}

static inline uint32_t to_pwm_level(int32_t val, uint32_t wrap) {
    if (val < 0) val = 0;
    if (val > 65535) val = 65535;
    uint32_t level = ((uint32_t)val * (wrap - 1)) / 65535;
    if (level >= wrap) level = wrap - 1;
    return level;
}

void signal_gen_core1_entry(void) {
    uint32_t phase_acc = 0;
    float ecg_out[GAUSSIAN_MODEL_NUM_LEADS];
    bool prev_r_wave = false;

    while (true) {
        if (is_running) {
            uint32_t clock_rate = clock_get_hz(clk_sys);
            uint32_t wrap = clock_rate / 100000;

            if (current_signal_type == SIGNAL_TYPE_ECG) {
                float ppg = gaussian_model_step(ecg_out);
                // RA-referenced pin values: GPIO1 = lead I, GPIO2 = lead II,
                // GPIO3..9 = V1..V6 (see the file header and
                // gaussian_model_compute_output_electrodes).
                float el_out[GAUSSIAN_MODEL_NUM_LEADS];
                gaussian_model_compute_output_electrodes(ecg_out, el_out);

                // counts per mV = amplitude% * 2.5 * calibration gain (match 02_)
                float counts_per_mV = ((float)signal_amplitude * 2.5f) * signal_cal_gain;
                int32_t ecg_pwm_val[GAUSSIAN_MODEL_NUM_LEADS];

                for (int col = 0; col < GAUSSIAN_MODEL_NUM_LEADS; col++) {
                    int32_t val;

                    if (col == RA_PIN_INDEX) {
                        // GPIO0 is a unipolar PPG output; use the measured
                        // safe ECG output interval as its baseline and span.
                        int32_t floor_val = (int32_t)ppg_ra_pwm_floor;
                        int32_t span = (int32_t)ppg_ra_pwm_ceil - floor_val;
                        float duty01 = ppg * ppg_out_scale;
                        val = floor_val + (int32_t)(duty01 * (float)span);
                        if (val < floor_val) val = floor_val;
                        if (val > (int32_t)ppg_ra_pwm_ceil) val = ppg_ra_pwm_ceil;
                    } else {
                        val = 32768
                            + (int32_t)(el_out[col] * counts_per_mV)
                            + (int32_t)ecg_calibration_offset
                            + (int32_t)signal_offset;
                        if (val < (int32_t)ecg_output_min) val = ecg_output_min;
                        if (val > (int32_t)ecg_output_max) val = ecg_output_max;
                    }

                    ecg_pwm_val[col] = val;
                    uint32_t level = to_pwm_level(val, wrap);
                    if (col < NUM_SIGNAL_PINS) {
                        uint pin = signal_pins[col];
                        uint slice_num = pwm_gpio_to_slice_num(pin);
                        uint channel = pwm_gpio_to_channel(pin);
                        pwm_set_chan_level(slice_num, channel, (uint16_t)level);
                    }
                }

                // Dedicated PPG monitor channel (GPIO10): same calibrated
                // ceiling as the RA pin, so both channels are identical and
                // can be compared on an oscilloscope.
                {
                    int32_t floor_val = (int32_t)ppg_ra_pwm_floor;
                    int32_t span = (int32_t)ppg_ra_pwm_ceil - floor_val;
                    float duty01 = ppg * ppg_out_scale;
                    int32_t ppg_val = floor_val + (int32_t)(duty01 * (float)span);
                    if (ppg_val < floor_val) ppg_val = floor_val;
                    if (ppg_val > (int32_t)ppg_ra_pwm_ceil) ppg_val = ppg_ra_pwm_ceil;
                    uint32_t ppg_level = to_pwm_level(ppg_val, wrap);
                    uint slice_num = pwm_gpio_to_slice_num(PPG_PIN);
                    uint channel = pwm_gpio_to_channel(PPG_PIN);
                    pwm_set_chan_level(slice_num, channel, (uint16_t)ppg_level);
                }

                // Lead II (GPIO2) is a robust R-peak indicator for the LED
                // and buzzer.
                bool r_wave = el_out[2] > 0.5f;
                if (r_wave && !prev_r_wave) {
                    interface_trigger_heartbeat();
                }
                prev_r_wave = r_wave;

                if (stream_mode_active) {
                    float stream_buf[STREAM_NUM_CHANNELS];
                    // [0] = model RA electrode potential (the board reference;
                    //       not driven on any pin, streamed for diagnostics and
                    //       exact 12-lead reconstruction).
                    stream_buf[0] = ecg_out[0];
                    // [1..8] = the actual pin values: the calibrated post-PWM
                    //         electrode voltage when startup calibration
                    //         succeeded, otherwise the raw RA-referenced model
                    //         value (no clamp).
                    for (int i = 1; i < GAUSSIAN_MODEL_NUM_LEADS
                         && i < STREAM_NUM_CHANNELS; i++) {
                        stream_buf[i] = startup_calibration_valid
                            ? pwm_value_to_stream_mv(ecg_pwm_val[i])
                            : el_out[i];
                    }
                    // Stream the PPG as the last channel (index 9), in the same
                    // 0..~1 a.u. units used on the PWM PPG_PIN output.
                    stream_buf[GAUSSIAN_MODEL_NUM_LEADS] = ppg;
                    stream_buffer_add(stream_buf);
                }

            } else {
                int32_t val = 0;
                uint16_t phase = (uint16_t)(phase_acc >> 16);

                if (current_signal_type == SIGNAL_TYPE_SINE) {
                    val = generate_sine(phase);
                } else if (current_signal_type == SIGNAL_TYPE_TRIANGLE) {
                    val = generate_triangle(phase);
                } else if (current_signal_type == SIGNAL_TYPE_SQUARE) {
                    val = generate_square(phase);
                } else if (current_signal_type == SIGNAL_TYPE_PULSE) {
                    val = generate_pulse(phase);
                }

                val += (int32_t)signal_offset;
                uint32_t level = to_pwm_level(val, wrap);

                for (int i = 0; i < NUM_SIGNAL_PINS; i++) {
                    uint pin = signal_pins[i];
                    uint slice_num = pwm_gpio_to_slice_num(pin);
                    uint channel = pwm_gpio_to_channel(pin);
                    pwm_set_chan_level(slice_num, channel, (uint16_t)level);
                }

                uint32_t prev_acc = phase_acc;
                uint64_t inc64 = ((uint64_t)signal_bpm << 32) / 120000;
                phase_acc += (uint32_t)inc64;

                if (prev_acc < 0x80000000 && phase_acc >= 0x80000000) {
                    interface_trigger_heartbeat();
                }
            }

            sleep_us(500); // Fixed 2000 Hz sample rate for the model and test waveforms
        } else {
            sleep_ms(10);
            phase_acc = 0;
        }
    }
}

