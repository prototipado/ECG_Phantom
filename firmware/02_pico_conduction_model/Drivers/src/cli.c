#include "cli.h"
#include "menu.h"
#include "signal_gen.h"
#include "cardiac_model.h"
#include "streaming.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CLI_BUF_SIZE 128

static char cmd_buf[CLI_BUF_SIZE];
static uint8_t cmd_len = 0;
static bool collecting = false;
static absolute_time_t cli_output_until;

// ==================== Forward Declarations ====================
static void cli_execute(const char* input);
static void cli_cmd_help(const char* args);
static void cli_cmd_set(const char* args);
static void cli_cmd_get(const char* args);
static void cli_cmd_list(const char* args);
static void cli_cmd_play(const char* args);
static void cli_cmd_stop(const char* args);
static void cli_cmd_ver(const char* args);
static void cli_cmd_stream(const char* args);
static void cli_cmd_info(const char* args);

// ==================== Command Table ====================
typedef struct {
    const char* name;
    const char* help;
    void (*handler)(const char* args);
} cli_command_t;

static const cli_command_t commands[] = {
    { "help",  "Show available commands",             cli_cmd_help  },
    { "set",   "Set a parameter (see 'help')",         cli_cmd_set   },
    { "get",   "Show current settings",                cli_cmd_get   },
    { "list",  "List available rhythm presets",         cli_cmd_list  },
    { "play",  "Start signal generation",               cli_cmd_play  },
    { "stop",  "Stop signal generation",                cli_cmd_stop  },
    { "ver",   "Show firmware version",                 cli_cmd_ver   },
    { "stream","Stream data (on/off)",                  cli_cmd_stream},
    { "info",  "Show project info",                     cli_cmd_info  },
};
#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

// ==================== Public API ====================

void cli_init(void) {
    cmd_len = 0;
    collecting = false;
    printf("\r\nCLI ready. Type '> help' for commands.\r\n");
    fflush(stdout);
}

void cli_feed_char(int c) {
    if (!collecting && c == '>') {
        collecting = true;
        cmd_len = 0;
        return;
    }

    if (c == '\r' || c == '\n') {
        if (cmd_len > 0) {
            cmd_buf[cmd_len] = '\0';
            printf("\r\n");
            cli_execute(cmd_buf);
            cli_output_until = make_timeout_time_ms(800);
        }
        cmd_len = 0;
        collecting = false;
        printf("\r\n> ");
        fflush(stdout);
        return;
    }

    if (c == 0x7F || c == '\b') {
        if (cmd_len > 0) {
            cmd_len--;
            printf("\b \b");
            fflush(stdout);
        }
        return;
    }

    if (c == 0x1B) {
        cmd_len = 0;
        collecting = false;
        printf("\r\n> ");
        fflush(stdout);
        return;
    }

    if (cmd_len < CLI_BUF_SIZE - 1 && c >= 0x20 && c <= 0x7E) {
        cmd_buf[cmd_len++] = (char)c;
        printf("%c", c);
        fflush(stdout);
    }
}

bool cli_is_collecting(void) {
    return collecting;
}

bool cli_output_active(void) {
    return absolute_time_diff_us(get_absolute_time(), cli_output_until) > 0;
}

// ==================== Internal Helpers ====================

static char* skip_whitespace(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static char* find_arg(char* input) {
    char* space = strchr(input, ' ');
    if (space) {
        *space = '\0';
        return skip_whitespace(space + 1);
    }
    return NULL;
}

static void cli_execute(const char* input) {
    char work[CLI_BUF_SIZE];
    strncpy(work, input, CLI_BUF_SIZE - 1);
    work[CLI_BUF_SIZE - 1] = '\0';

    char* cmd = skip_whitespace(work);
    if (*cmd == '\0') return;

    char* args = find_arg(cmd);

    for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            commands[i].handler(args);
            return;
        }
    }

    printf("Unknown command: '%s'. Type '> help' for commands.\r\n", cmd);
    fflush(stdout);
}

// ==================== Command Handlers ====================

static void cli_cmd_help(const char* args) {
    (void)args;
    printf("Available commands:\r\n");
    for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
        printf("  %-8s %s\r\n", commands[i].name, commands[i].help);
    }
    printf("\r\nSet params: type, hr, amp, offset, rhythm, hrv, hrvdepth,\r\n");
    printf("            cal, baseamp, basefreq, a1, a2, a3, a4, delay, buzzer, led\r\n");
    printf("Signal types: ecg, sine, tri, sq, pulse\r\n");
    fflush(stdout);
}

static void cli_cmd_set(const char* args) {
    if (!args || *args == '\0') {
        printf("Usage: > set <param> <value>\r\n");
        fflush(stdout);
        return;
    }

    char work[CLI_BUF_SIZE];
    strncpy(work, args, CLI_BUF_SIZE - 1);
    work[CLI_BUF_SIZE - 1] = '\0';

    char* param = skip_whitespace(work);
    char* val_str = find_arg(param);

    if (!val_str || *val_str == '\0') {
        printf("Missing value for '%s'\r\n", param);
        fflush(stdout);
        return;
    }

    if (strcmp(param, "type") == 0) {
        if (strcmp(val_str, "ecg") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_ECG);
            signal_gen_set_amplitude(settings.amplitude * 240);
            printf("Signal type: Cardiac model\r\n");
        } else if (strcmp(val_str, "sine") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_SINE);
            signal_gen_set_amplitude(settings.amplitude * 327);
            printf("Signal type: Sine\r\n");
        } else if (strcmp(val_str, "tri") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_TRIANGLE);
            signal_gen_set_amplitude(settings.amplitude * 327);
            printf("Signal type: Triangle\r\n");
        } else if (strcmp(val_str, "sq") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_SQUARE);
            signal_gen_set_amplitude(settings.amplitude * 327);
            printf("Signal type: Square\r\n");
        } else if (strcmp(val_str, "pulse") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_PULSE);
            signal_gen_set_amplitude(settings.amplitude * 327);
            printf("Signal type: Pulse\r\n");
        } else {
            printf("Unknown type: '%s'. Use: ecg, sine, tri, sq, pulse\r\n", val_str);
        }
    } else if (strcmp(param, "hr") == 0) {
        int val = atoi(val_str);
        if (val < 15 || val > 250) {
            printf("Invalid heart rate. Range: 15-250\r\n");
        } else {
            settings.heart_rate = (uint16_t)val;
            signal_gen_set_bpm(settings.heart_rate);
            printf("Heart rate: %u bpm\r\n", settings.heart_rate);
        }
    } else if (strcmp(param, "amp") == 0) {
        int val = atoi(val_str);
        if (val < 0 || val > 100) {
            printf("Invalid amplitude. Range: 0-100\r\n");
        } else {
            settings.amplitude = (uint16_t)val;
            signal_type_t type = signal_gen_get_signal_type();
            signal_gen_set_amplitude(settings.amplitude * (type == SIGNAL_TYPE_ECG ? 240 : 327));
            printf("Amplitude: %u%%\r\n", settings.amplitude);
        }
    } else if (strcmp(param, "offset") == 0) {
        int val = atoi(val_str);
        if (val < -16384 || val > 16384) {
            printf("Invalid offset. Range: -16384 to 16384\r\n");
        } else {
            settings.offset = (int16_t)val;
            signal_gen_set_offset(settings.offset);
            printf("Offset: %d\r\n", settings.offset);
        }
    } else if (strcmp(param, "rhythm") == 0) {
        int val = atoi(val_str);
        uint8_t count = cardiac_model_get_rhythm_count();
        if (val < 0 || val >= count) {
            printf("Invalid rhythm index. Range: 0-%d\r\n", count - 1);
        } else {
            cardiac_model_apply_rhythm((uint8_t)val);
            settings.rhythm_index = (uint8_t)val;
            printf("Rhythm: %s\r\n", cardiac_model_get_rhythm_name((uint8_t)val));
        }
    } else if (strcmp(param, "hrv") == 0) {
        cardiac_model_params.hrv_enabled = (strcmp(val_str, "on") == 0);
        printf("HRV: %s\r\n", cardiac_model_params.hrv_enabled ? "on" : "off");
    } else if (strcmp(param, "hrvdepth") == 0) {
        cardiac_model_params.hrv_depth = strtof(val_str, NULL);
        printf("HRV depth: %.2f\r\n", (double)cardiac_model_params.hrv_depth);
    } else if (strcmp(param, "baseamp") == 0) {
        cardiac_model_params.baseline_z0_mV = strtof(val_str, NULL);
        printf("Baseline amp: %.2f mV\r\n", (double)cardiac_model_params.baseline_z0_mV);
    } else if (strcmp(param, "basefreq") == 0) {
        cardiac_model_params.baseline_f0_hz = strtof(val_str, NULL);
        printf("Baseline freq: %.2f Hz\r\n", (double)cardiac_model_params.baseline_f0_hz);
    } else if (strcmp(param, "a1") == 0) {
        cardiac_model_params.alpha1 = strtof(val_str, NULL);
        printf("Alpha1 (P): %.2f\r\n", (double)cardiac_model_params.alpha1);
    } else if (strcmp(param, "a2") == 0) {
        cardiac_model_params.alpha2 = strtof(val_str, NULL);
        printf("Alpha2 (Ta): %.2f\r\n", (double)cardiac_model_params.alpha2);
    } else if (strcmp(param, "a3") == 0) {
        cardiac_model_params.alpha3 = strtof(val_str, NULL);
        printf("Alpha3 (QRS): %.2f\r\n", (double)cardiac_model_params.alpha3);
    } else if (strcmp(param, "a4") == 0) {
        cardiac_model_params.alpha4 = strtof(val_str, NULL);
        printf("Alpha4 (T): %.2f\r\n", (double)cardiac_model_params.alpha4);
    } else if (strcmp(param, "delay") == 0) {
        cardiac_model_params.tau_node_ms = strtof(val_str, NULL);
        printf("Node delay: %.2f ms\r\n", (double)cardiac_model_params.tau_node_ms);
    } else if (strcmp(param, "buzzer") == 0) {
        settings.buzzer_enabled = (strcmp(val_str, "on") == 0);
        printf("Buzzer: %s\r\n", settings.buzzer_enabled ? "ON" : "OFF");
    } else if (strcmp(param, "led") == 0) {
        settings.led_enabled = (strcmp(val_str, "on") == 0);
        printf("LED: %s\r\n", settings.led_enabled ? "ON" : "OFF");
    } else if (strcmp(param, "cal") == 0) {
        char* end = NULL;
        float gain = strtof(val_str, &end);
        while (end && (*end == ' ' || *end == '\t')) end++;
        if (end == val_str || (end && *end != '\0') ||
            !(gain >= 0.01f && gain <= 100.0f)) {
            printf("Invalid cal gain. Range: 0.01 to 100\r\n");
        } else {
            signal_gen_set_cal_gain(gain);
            printf("Cal gain: %.3f (mV-to-counts multiplier)\r\n",
                   (double)signal_gen_get_cal_gain());
        }
    } else {
        printf("Unknown parameter: '%s'. Type '> help'\r\n", param);
    }
    fflush(stdout);
}

static void cli_cmd_get(const char* args) {
    (void)args;
    const char* type_str = "?";
    switch (signal_gen_get_signal_type()) {
        case SIGNAL_TYPE_ECG:      type_str = "ecg (model)"; break;
        case SIGNAL_TYPE_SINE:     type_str = "sine";        break;
        case SIGNAL_TYPE_TRIANGLE: type_str = "tri";         break;
        case SIGNAL_TYPE_SQUARE:   type_str = "sq";          break;
        case SIGNAL_TYPE_PULSE:    type_str = "pulse";       break;
    }
    printf("Settings:\r\n");
    printf("  type      = %s\r\n", type_str);
    printf("  hr        = %u bpm\r\n", settings.heart_rate);
    printf("  amp       = %u%%\r\n", settings.amplitude);
    printf("  offset    = %d\r\n", settings.offset);
    printf("  rhythm    = %d (%s)\r\n", settings.rhythm_index, cardiac_model_get_rhythm_name(settings.rhythm_index));
    printf("  hrv       = %s (depth %.2f)\r\n", cardiac_model_params.hrv_enabled ? "on" : "off", (double)cardiac_model_params.hrv_depth);
    printf("  hrvdepth  = %.2f\r\n", (double)cardiac_model_params.hrv_depth);
    printf("  baseline  = %.2f mV @ %.2f Hz\r\n", (double)cardiac_model_params.baseline_z0_mV, (double)cardiac_model_params.baseline_f0_hz);
    printf("  alphas    = P:%.2f Ta:%.2f QRS:%.2f T:%.2f\r\n",
           (double)cardiac_model_params.alpha1, (double)cardiac_model_params.alpha2,
           (double)cardiac_model_params.alpha3, (double)cardiac_model_params.alpha4);
    printf("  delay     = %.2f ms\r\n", (double)cardiac_model_params.tau_node_ms);
    printf("  buzzer    = %s\r\n", settings.buzzer_enabled ? "on" : "off");
    printf("  led       = %s\r\n", settings.led_enabled ? "on" : "off");
    printf("  cal       = %.3f (mV-to-counts multiplier)\r\n", (double)signal_gen_get_cal_gain());
    fflush(stdout);
}

static void cli_cmd_list(const char* args) {
    (void)args;
    uint8_t count = cardiac_model_get_rhythm_count();
    printf("Rhythm presets (%d):\r\n", count);
    for (uint8_t i = 0; i < count; i++) {
        printf("  [%d] %s\r\n", i, cardiac_model_get_rhythm_name(i));
    }
    fflush(stdout);
}

static void cli_cmd_play(const char* args) {
    (void)args;
    signal_gen_start();
    settings.is_playing = true;
    printf("Signal generation: STARTED\r\n");
    fflush(stdout);
}

static void cli_cmd_stop(const char* args) {
    (void)args;
    signal_gen_stop();
    settings.is_playing = false;
    printf("Signal generation: STOPPED\r\n");
    fflush(stdout);
}

static void cli_cmd_ver(const char* args) {
    (void)args;
    printf("02_pico_conduction_model v1.1 (2026)\r\n");
    printf("Board: RP2350 (Pico 2)\r\n");
    printf("Model: Quiroz-Juarez et al. 2018 heterogeneous oscillator conduction model\r\n");
    fflush(stdout);
}

void cli_start_collecting(void) {
    collecting = true;
    cmd_len = 0;
}

bool cli_is_streaming(void) {
    return stream_mode_active;
}

void cli_set_streaming(bool on) {
    stream_mode_active = on;
    if (on) {
        stream_buffer.write_idx = 0;
        stream_buffer.read_idx = 0;
    }
}

static void cli_cmd_stream(const char* args) {
    if (!args || *args == '\0') {
        printf("Usage: > stream on|off\r\n");
        fflush(stdout);
        return;
    }
    if (strcmp(args, "on") == 0) {
        cli_set_streaming(true);
        printf("STREAM ON\r\n");
        fflush(stdout);
    } else if (strcmp(args, "off") == 0) {
        cli_set_streaming(false);
        printf("STREAM OFF\r\n");
        fflush(stdout);
    } else {
        printf("Usage: > stream on|off\r\n");
        fflush(stdout);
    }
}

static void cli_cmd_info(const char* args) {
    (void)args;
    printf("PROJECT=02_pico_conduction_model\r\n");
    printf("VERSION=1.1\r\n");
    printf("BOARD=RP2350 (Pico 2)\r\n");
    printf("MODEL=Quiroz-Juarez 2018 heterogeneous oscillator conduction model\r\n");
    printf("LEADS=9 (RA, LA/I, LL/II, V1-V6)\r\n");
    printf("SAMPLE_RATE=2000\r\n");
    printf("PARAMS=hr,amp,offset,cal,rhythm,hrv,hrvdepth,baseamp,basefreq,a1,a2,a3,a4,delay\r\n");
    printf("SIGNAL_TYPES=ecg,sine,tri,sq,pulse\r\n");
    fflush(stdout);
}
