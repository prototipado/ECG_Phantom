#include "signal_gen.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <limits.h>
#include "interface.h"
#include "cardiac_model.h"
#include "streaming.h"

stream_buffer_t stream_buffer;
volatile bool stream_mode_active = false;

// Pins: 0, 1, 2, 3, 4, 6, 7, 8, 9  ->  RA, LA(I), LL(II), V1, V2, V3, V4, V5, V6
static const uint signal_pins[] = {0, 1, 2, 3, 4, 6, 7, 8, 9};
#define NUM_SIGNAL_PINS (sizeof(signal_pins) / sizeof(signal_pins[0]))
#define MODEL_INTEGRATION_RATE_HZ 2000
#define STARTUP_CAL_POINTS 256
#define STARTUP_CAL_SETTLE_POINTS 64
#define STARTUP_CAL_AMPLITUDE 10000
#define STARTUP_CAL_MODEL_WARMUP_SAMPLES (2 * MODEL_INTEGRATION_RATE_HZ)
#define STARTUP_CAL_MODEL_SAMPLES (8 * MODEL_INTEGRATION_RATE_HZ)
#define STARTUP_CAL_SAFE_ADC_MIN 300
#define STARTUP_CAL_SAFE_ADC_MAX 3100
#define OUTPUT_DIVIDER_TOP_OHMS 100000.0f
#define OUTPUT_DIVIDER_BOTTOM_OHMS 1000.0f

static volatile bool is_running = true;
static volatile signal_type_t current_signal_type = SIGNAL_TYPE_ECG;
static volatile uint16_t signal_amplitude = 8000;  // 0-24000 nominal (see cardiac_model scaling)
static volatile uint16_t signal_bpm = 72;          // Latidos por minuto (BPM)
static volatile int16_t signal_offset = 0;  // Vertical offset in ADC units
static volatile int16_t ecg_calibration_offset = 0;
static volatile float signal_cal_gain = 1.0f;  // Output calibration multiplier (counts per mV)
static volatile uint16_t ecg_output_min = 0;
static volatile uint16_t ecg_output_max = 65535;
static volatile float calibration_adc_mean = 2047.5f;
static volatile float calibration_adc_counts_per_pwm_count = 0.0f;
static volatile bool startup_calibration_valid = false;

// 256-entry sine lookup table for one standard period (0 to 2*pi)
// Values scaled to 0-16384 (14-bit) centered at 0
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

void signal_gen_init(void) {
    for (int i = 0; i < NUM_SIGNAL_PINS; i++) {
        uint pin = signal_pins[i];
        gpio_set_function(pin, GPIO_FUNC_PWM);
        uint slice_num = pwm_gpio_to_slice_num(pin);
        
        // Configure PWM
        // For signal generation, we want high resolution but enough frequency
        // 100kHz as requested in Fantomas.ino
        uint32_t clock_rate = clock_get_hz(clk_sys);
        uint32_t frequency = 100000;
        // Exactly 100kHz: 125,000,000 / 100,000 = 1250
        // RP2350 default clock is usually 125MHz or 150MHz.
        // We use the hardware clock to calculate the wrap for exact 100kHz.
        uint32_t wrap = clock_rate / frequency;
        if (wrap < 1) wrap = 1;
        
        pwm_set_wrap(slice_num, wrap - 1);
        pwm_set_clkdiv(slice_num, 1.0f);
        pwm_set_enabled(slice_num, true);
    }
    cardiac_model_init((float)MODEL_INTEGRATION_RATE_HZ);
}

void signal_gen_start(void) {
    is_running = true;
}

void signal_gen_stop(void) {
    is_running = false;
}

void signal_gen_set_signal_type(signal_type_t type) {
    current_signal_type = type;
}

void signal_gen_set_amplitude(uint16_t amplitude) {
    // amplitude peak displacement from center (0-32768)
    if (amplitude > 32768) amplitude = 32768;
    signal_amplitude = amplitude;
}

void signal_gen_set_frequency(uint16_t frequency) {
    // Legacy support: convert frequency to BPM
    signal_gen_set_bpm(frequency * 60);
}

void signal_gen_set_bpm(uint16_t bpm) {
    // bpm in range 30-240
    if (bpm < 1) bpm = 1;
    if (bpm > 3000) bpm = 3000; // Allow high frequencies for testing
    signal_bpm = bpm;
    cardiac_model_set_heart_rate((float)bpm);
}

void signal_gen_set_offset(int16_t offset) {
    // offset: vertical shift in ADC units (-16384 to +16384)
    if (offset < -16384) offset = -16384;
    if (offset > 16384) offset = 16384;
    signal_offset = offset;
}

int16_t signal_gen_get_offset(void) {
    return signal_offset;
}

bool signal_gen_calibrate_startup(void) {
    const uint pin = signal_pins[0];
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
    float model_out[CARDIAC_MODEL_NUM_LEADS];
    startup_calibration_valid = false;
    ecg_calibration_offset = 0;

    printf("Startup calibration: RA sine on GPIO0, read ADC0/GPIO26...\r\n");
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
    float base_counts_per_mV = (float)signal_amplitude * 2.5f;

    cardiac_model_init((float)MODEL_INTEGRATION_RATE_HZ);
    cardiac_model_set_heart_rate((float)signal_bpm);
    for (int i = 0; i < STARTUP_CAL_MODEL_WARMUP_SAMPLES
         + STARTUP_CAL_MODEL_SAMPLES; i++) {
        cardiac_model_step(model_out);
        if (i < STARTUP_CAL_MODEL_WARMUP_SAMPLES) continue;
        for (int col = 1; col < CARDIAC_MODEL_NUM_LEADS; col++) {
            float unscaled_pwm = model_out[col] * base_counts_per_mV;
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
        cardiac_model_init((float)MODEL_INTEGRATION_RATE_HZ);
        cardiac_model_set_heart_rate((float)signal_bpm);
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

    unsigned int gain_milli = (unsigned int)(gain * 1000.0f + 0.5f);
    printf("Startup calibration OK: offset=%d, cal=%u.%03u, ADC target=%u..%u\r\n",
           ecg_calibration_offset, gain_milli / 1000, gain_milli % 1000,
           STARTUP_CAL_SAFE_ADC_MIN, STARTUP_CAL_SAFE_ADC_MAX);
    printf("Model range at nominal amplitude: %.2f..%.2f PWM counts\r\n",
           (double)model_min, (double)model_max);
    fflush(stdout);

    cardiac_model_init((float)MODEL_INTEGRATION_RATE_HZ);
    cardiac_model_set_heart_rate((float)signal_bpm);
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

signal_type_t signal_gen_get_signal_type(void) {
    return current_signal_type;
}

uint16_t signal_gen_get_bpm(void) {
    return signal_bpm;
}

// Generate sine wave value (0-32768 range)
static int32_t generate_sine(uint16_t phase) {
    // phase: 0 to 65535 representing 0 to 2*pi
    int32_t center = 32768;
    int32_t amplitude = (int32_t)signal_amplitude;
    
    // Look up value (0-255 index covers full cycle)
    uint8_t idx = (uint8_t)(phase >> 8);
    int32_t lut_val = (int32_t)sine_lut[idx];
    
    // sine_lut is +/- 16384. 
    // We scale by (amplitude / 16384)
    int32_t scaled_sine = (lut_val * amplitude) / 16384;
    
    return center + scaled_sine;
}

// Generate triangle wave value (0-32768 range)
static int32_t generate_triangle(uint16_t phase) {
    // phase: 0 to 65535 (0 to 2*pi)
    int32_t center = 32768;
    int32_t amplitude = (int32_t)signal_amplitude; 
    int32_t tri_val = 0;
    
    if (phase < 16384) {
        // 0 to pi/2: ramp 0 to +amplitude
        tri_val = ((int32_t)phase * amplitude) / 16384;
    } else if (phase < 49152) {
        // pi/2 to 3pi/2: ramp +amplitude to -amplitude
        tri_val = -(((int32_t)phase - 32768) * amplitude) / 16384;
    } else {
        // 3pi/2 to 2pi: ramp -amplitude to 0
        tri_val = (((int32_t)phase - 65536) * amplitude) / 16384;
    }
    
    return center + tri_val;
}

// Generate square wave value (0-32768 range)
static int32_t generate_square(uint16_t phase) {
    // phase: 0 to 65535
    int32_t center = 32768;
    int32_t amplitude = (int32_t)signal_amplitude;
    
    if (phase < 32768) {
        return center + amplitude;
    } else {
        return center - amplitude;
    }
}

// Generate pulse wave value (0-65535 range)
// Duty cycle is configurable (Default: 5%)
static int32_t generate_pulse(uint16_t phase) {
    // phase: 0 to 65535
    int32_t center = 32768;
    int32_t amplitude = (int32_t)signal_amplitude;
    
    // Duty cycle parameter: 5% of 65536 is ~3277
    // Change this value to adjust the pulse width
    const uint16_t pulse_threshold = 3277; 
    
    if (phase < pulse_threshold) {
        return center + amplitude;
    } else {
        return center - amplitude;
    }
}

void signal_gen_core1_entry(void) {
    uint32_t phase_acc = 0; // 32-bit for high precision (test waveforms)
    float model_out[CARDIAC_MODEL_NUM_LEADS];
    bool prev_r_wave = false;

    while (true) {
        if (is_running) {
            uint32_t clock_rate = clock_get_hz(clk_sys);
            uint32_t wrap = clock_rate / 100000;

            if (current_signal_type == SIGNAL_TYPE_ECG) {
                // Advance the ODE model by one sample and read the 9 electrode
                // potentials (RA, LA/I, LL/II, V1..V6) it produces.
                cardiac_model_step(model_out);

                // counts per mV = (amplitude%/8) * calibration gain.
                // Match the legacy ECG path: its raw samples are about 1000
                // counts/mV and were amplified by signal_amplitude / 400.
                // This is equivalent to signal_amplitude * 2.5 PWM counts/mV.
                float counts_per_mV = ((float)signal_amplitude * 2.5f) * signal_cal_gain;
                float stream_buf[STREAM_NUM_CHANNELS] = {0};

                for (int col = 0; col < CARDIAC_MODEL_NUM_LEADS; col++) {
                    float ecg_mV = model_out[col];
                    int32_t val = 32768 + (int32_t)(ecg_mV * counts_per_mV)
                        + (int32_t)ecg_calibration_offset
                        + (int32_t)signal_offset;
                    if (val < 0) val = 0;
                    if (val > 65535) val = 65535;
                    if (val < (int32_t)ecg_output_min) val = ecg_output_min;
                    if (val > (int32_t)ecg_output_max) val = ecg_output_max;
                    uint32_t level = ((uint32_t)val * (wrap - 1)) / 65535;
                    if (level >= wrap) level = wrap - 1;

                    if (col < NUM_SIGNAL_PINS) {
                        uint pin = signal_pins[col];
                        uint slice_num = pwm_gpio_to_slice_num(pin);
                        uint channel = pwm_gpio_to_channel(pin);
                        if (col == 0) {
                            // RA is the reference electrode (0V reference in this setup)
                            pwm_set_chan_level(slice_num, channel, 0);
                        } else {
                            pwm_set_chan_level(slice_num, channel, (uint16_t)level);
                        }
                    }
                    if (col > 0 && startup_calibration_valid) {
                        stream_buf[col] = pwm_value_to_stream_mv(val);
                    } else if (col > 0) {
                        stream_buf[col] = ecg_mV;
                    }
                }

                // Heartbeat indication approximated from the Lead II (index 2)
                // rising edge crossing a fixed threshold near the R-peak.
                bool r_wave = model_out[2] > 0.5f;
                if (r_wave && !prev_r_wave) {
                    interface_trigger_heartbeat();
                }
                prev_r_wave = r_wave;

                if (stream_mode_active) {
                    stream_buffer_add(stream_buf);
                }

            } else {
                // Generate test waveform (Sine/Triangle/Square/Pulse)
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
                if (val < 0) val = 0;
                if (val > 65535) val = 65535;

                uint32_t level = ((uint32_t)val * (wrap - 1)) / 65535;

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

            sleep_us(500); // Fixed 2000 Hz sample rate for model and test waveforms
        } else {
            // Idle
            sleep_ms(10);
            phase_acc = 0;
        }
    }
}
