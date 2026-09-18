#include "signal_gen.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include <stdio.h>
#include "interface.h"
#include "streaming.h"

// Streaming ring-buffer and mode flag (written by Core 1, read by Core 0)
stream_buffer_t stream_buffer;
volatile bool stream_mode_active = false;

// Pins: 0, 1, 2, 3, 4, 6, 7, 8, 9
static const uint signal_pins[] = {0, 1, 2, 3, 4, 6, 7, 8, 9};
#define NUM_SIGNAL_PINS (sizeof(signal_pins) / sizeof(signal_pins[0]))

static volatile const ecg_struct_t* active_signal = NULL;
static volatile bool is_running = true;
static volatile signal_type_t current_signal_type = SIGNAL_TYPE_ECG;
static volatile uint16_t signal_amplitude = 8000;  // 0-16384 (half of 32768)
static volatile uint16_t signal_bpm = 60;          // Latidos por minuto (BPM)
static volatile int16_t signal_offset = 0;  // Vertical offset in ADC units
static uint32_t sampling_period_us = 500; // Default 500us (2000Hz) for periodic signals

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
}

void signal_gen_set_signal(const ecg_struct_t* signal) {
    active_signal = signal;
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
    // Periodic signals sampled at 2000Hz (500us)
    sampling_period_us = 500; 
}

void signal_gen_set_offset(int16_t offset) {
    // offset: vertical shift in ADC units (-16384 to +16384)
    if (offset < -16384) offset = -16384;
    if (offset > 16384) offset = 16384;
    signal_offset = offset;
}

signal_type_t signal_gen_get_signal_type(void) {
    return current_signal_type;
}

uint16_t signal_gen_get_bpm(void) {
    if (current_signal_type == SIGNAL_TYPE_ECG) {
        if (active_signal != NULL && active_signal->rows > 0) {
            // BPM = (60sec * 500Hz) / rows
            return (uint16_t)(30000 / active_signal->rows);
        }
        return 60;
    } else {
        // For waveforms, BPM is directly set
        return signal_bpm;
    }
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
    int current_row = 0;
    uint32_t phase_acc = 0; // 32-bit for high precision
    int debug_counter = 0;
    const int DEBUG_INTERVAL = 100;  // Print every 100 samples
    absolute_time_t next_sample_time = get_absolute_time();

    while (true) {
        if (is_running) {
            // Sampling period: 2000 us = 500 Hz (matches 500 Hz LUDB ECG data)
            uint32_t period_us = 2000;
            absolute_time_t now = get_absolute_time();
            if (absolute_time_diff_us(next_sample_time, now) > (int32_t)(period_us * 2)) {
                next_sample_time = now;
            }
            next_sample_time = delayed_by_us(next_sample_time, period_us);

            switch (current_signal_type) {
                case SIGNAL_TYPE_ECG: {
                    // Use loaded ECG signal (like Fantomas.ino does)
                    // Values in signal headers are in 15-bit range (roughly 0-32768)
                    if (active_signal != NULL) {
                        const int* data_row = (const int*)active_signal->array + (current_row * active_signal->cols);
                        uint32_t clock_rate = clock_get_hz(clk_sys);
                        uint32_t wrap = clock_rate / 100000;

                        // Set PWM outputs and Print CSV
                        for (int col = 0; col < active_signal->cols; col++) {
                            int32_t val = (int32_t)data_row[col];
                            
                            // AC Amplification logic:
                            // 1. Subtract approximate baseline (30000)
                            int32_t ac_component = val - 30000;
                            // 2. Amplify using signal_amplitude (0-16384)
                            // Gain = signal_amplitude / 400 (Default 8000 -> Gain 20)
                            int32_t amplified = (ac_component * (int32_t)signal_amplitude) / 400;
                            // 3. Re-center in 16-bit range (32768) and clamp
                            int32_t final_val = 32768 + amplified;
                            if (final_val < 0) final_val = 0;
                            if (final_val > 65535) final_val = 65535;

                            // Scale for current PWM wrap capacity
                            uint32_t level = ((uint32_t)final_val * (wrap - 1)) / 65535;
                            if (level >= wrap) level = wrap - 1;

                            // Set PWM output for associated pin based on direct mapping:
                            // Column 0 -> RA (GPIO 0) -> ALWAYS 0 as reference
                            // Column 1 -> LA (GPIO 1)
                            // Column 2 -> LL (GPIO 2)
                            // Column 3 -> V1 (GPIO 3) ... and so on
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
                        }

                        // --- Streaming: push normalised mV values to ring buffer ---
                        if (stream_mode_active) {
                            float stream_buf[STREAM_NUM_CHANNELS];
                            // Convert raw 15-bit values to millivolts (baseline ~30000, ~1000 counts/mV).
                            for (int s = 0; s < active_signal->cols && s < STREAM_NUM_CHANNELS; s++) {
                                int32_t raw = (int32_t)((const int*)active_signal->array)[current_row * active_signal->cols + s];
                                stream_buf[s] = (float)(raw - 30000) / 1000.0f;
                            }
                            for (int s = active_signal->cols; s < STREAM_NUM_CHANNELS; s++) {
                                stream_buf[s] = 0.0f;
                            }
                            stream_buffer_add(stream_buf);
                        }

                        current_row++;
                        if (current_row >= active_signal->rows) {
                            current_row = 0;
                        }
                        
                        // Trigger heartbeat indication at the "R" peak (approx middle of signal)
                        if (current_row == active_signal->rows / 2) {
                            interface_trigger_heartbeat();
                        }
                    }
                    break;
                }
                
                case SIGNAL_TYPE_SINE:
                case SIGNAL_TYPE_TRIANGLE:
                case SIGNAL_TYPE_SQUARE:
                case SIGNAL_TYPE_PULSE: {
                    // Generate waveform
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
                    
                    // Apply global offset
                    val += (int32_t)signal_offset;
                    if (val < 0) val = 0;
                    if (val > 65535) val = 65535;
                    
                    uint32_t clock_rate = clock_get_hz(clk_sys);
                    uint32_t wrap = clock_rate / 100000;
                    uint32_t level = ((uint32_t)val * (wrap - 1)) / 65535;
                    
                    for (int i = 0; i < NUM_SIGNAL_PINS; i++) {
                        uint pin = signal_pins[i];
                        uint slice_num = pwm_gpio_to_slice_num(pin);
                        uint channel = pwm_gpio_to_channel(pin);
                        pwm_set_chan_level(slice_num, channel, (uint16_t)level);
                    }
                    // Push normalised mV value to streaming buffer
                    if (stream_mode_active) {
                        float stream_buf[STREAM_NUM_CHANNELS];
                        // Convert 0-65535 val to mV range (center 32768 = 0.0 mV, range ±1.0 mV)
                        float float_mv = (float)(val - 32768) / 16384.0f;
                        for (int s = 0; s < STREAM_NUM_CHANNELS; s++) {
                            stream_buf[s] = float_mv;
                        }
                        stream_buffer_add(stream_buf);
                    }

                    // High precision phase increment based on BPM at 500 Hz sampling rate
                    // Increment = (BPM / 60) * (2^32 / 500) = (BPM * 2^32) / 30000
                    uint32_t prev_acc = phase_acc;
                    uint64_t inc64 = ((uint64_t)signal_bpm << 32) / 30000;
                    phase_acc += (uint32_t)inc64;
                    
                    // Trigger heartbeat indication at the midpoint of the cycle (180 degrees)
                    if (prev_acc < 0x80000000 && phase_acc >= 0x80000000) {
                        interface_trigger_heartbeat();
                    }
                    break;
                }
            }
            
            debug_counter++;
            if (debug_counter >= DEBUG_INTERVAL) {
                debug_counter = 0;
            }
            
            // Sleep until target timestamp to guarantee exact 500 Hz sample rate without loop drift
            sleep_until(next_sample_time);
        } else {
            // Idle
            sleep_ms(10);
            current_row = 0;
            phase_acc = 0;
            next_sample_time = get_absolute_time();
        }
    }
}
