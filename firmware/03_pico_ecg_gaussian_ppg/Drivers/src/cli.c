#include "cli.h"
#include "menu.h"
#include "signal_gen.h"
#include "gaussian_ecg_ppg_model.h"
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
static void cli_cmd_rhythm(const char* args);
static void cli_cmd_defaults(const char* args);
static void cli_cmd_status(const char* args);

// ==================== Command Table ====================
typedef struct {
    const char* name;
    const char* help;
    void (*handler)(const char* args);
} cli_command_t;

static const cli_command_t commands[] = {
    { "help",  "Show available commands",     cli_cmd_help  },
    { "set",   "Set a parameter (see help)",  cli_cmd_set   },
    { "get",   "Show current settings",       cli_cmd_get   },
    { "list",  "List rhythm presets",         cli_cmd_list  },
    { "play",  "Start signal generation",     cli_cmd_play  },
    { "stop",  "Stop signal generation",      cli_cmd_stop  },
    { "ver",   "Show firmware version",       cli_cmd_ver   },
    { "status","Show status",                 cli_cmd_status},
    { "stream","Stream data (on/off)",        cli_cmd_stream},
    { "info",  "Show project info",           cli_cmd_info  },
    { "rhythm","Set rhythm preset",           cli_cmd_rhythm},
    { "defaults","Reset all parameters to factory boot config", cli_cmd_defaults},
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
    printf("\r\nCommon params: type, hr, amp, offset, rhythm, buzzer, led\r\n");
    printf("Signal types: ecg, sine, tri, sq, pulse\r\n");
    printf("Conduction:  pr, princ, escape, respdepth, respfreq, qtexp\r\n");
    printf("ECG wave:    pamp, pmu, psigma, qamp, ramp, samp, tamp, tmu, tsigma\r\n");
    printf("PPG coupling: pep, len, pwv, ectamp, ectwiden, ectdelay, ectpep, ppgamp\r\n");
    fflush(stdout);
}

static void cli_cmd_set(const char* args) {
    if (!args || *args == '\0') {
        printf("Usage: > set <param> <value>. Type '> help' for the list.\r\n");
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
            printf("Signal type: ECG (Gaussian model)\r\n");
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
        uint8_t count = gaussian_model_get_rhythm_count();
        if (val < 0 || val >= count) {
            printf("Invalid rhythm index. Range: 0-%d\r\n", count - 1);
        } else {
            gaussian_model_apply_rhythm((uint8_t)val);
            settings.rhythm_index = (uint8_t)val;
            settings.heart_rate = (uint16_t)gaussian_model_params.atrial_rate_bpm;
            signal_gen_set_bpm(settings.heart_rate);
            printf("Rhythm: %s\r\n", gaussian_model_get_rhythm_name((uint8_t)val));
        }
    } else if (strcmp(param, "pr") == 0) {
        gaussian_model_params.pr_interval_ms = strtof(val_str, NULL);
        printf("PR interval: %.1f ms\r\n", (double)gaussian_model_params.pr_interval_ms);
    } else if (strcmp(param, "princ") == 0) {
        gaussian_model_params.pr_increment_ms = strtof(val_str, NULL);
        printf("Mobitz PR increment: %.1f ms\r\n", (double)gaussian_model_params.pr_increment_ms);
    } else if (strcmp(param, "escape") == 0) {
        gaussian_model_params.escape_rate_bpm = strtof(val_str, NULL);
        printf("Escape rate: %.1f bpm\r\n", (double)gaussian_model_params.escape_rate_bpm);
    } else if (strcmp(param, "respdepth") == 0) {
        gaussian_model_params.resp_mod_depth = strtof(val_str, NULL);
        printf("RSA depth: %.3f\r\n", (double)gaussian_model_params.resp_mod_depth);
    } else if (strcmp(param, "respfreq") == 0) {
        gaussian_model_params.resp_freq_hz = strtof(val_str, NULL);
        printf("RSA freq: %.3f Hz\r\n", (double)gaussian_model_params.resp_freq_hz);
    } else if (strcmp(param, "qtexp") == 0) {
        gaussian_model_params.qt_rate_exponent = strtof(val_str, NULL);
        printf("QT rate exponent: %.3f\r\n", (double)gaussian_model_params.qt_rate_exponent);
    } else if (strcmp(param, "pamp") == 0) {
        gaussian_model_params.p_amp = strtof(val_str, NULL);
        printf("P amplitude: %.3f\r\n", (double)gaussian_model_params.p_amp);
    } else if (strcmp(param, "pmu") == 0) {
        gaussian_model_params.p_mu_ms = strtof(val_str, NULL);
        printf("P offset: %.1f ms\r\n", (double)gaussian_model_params.p_mu_ms);
    } else if (strcmp(param, "psigma") == 0) {
        gaussian_model_params.p_sigma_ms = strtof(val_str, NULL);
        printf("P width: %.1f ms\r\n", (double)gaussian_model_params.p_sigma_ms);
    } else if (strcmp(param, "qamp") == 0) {
        gaussian_model_params.q_amp = strtof(val_str, NULL);
        printf("Q amplitude: %.3f\r\n", (double)gaussian_model_params.q_amp);
    } else if (strcmp(param, "ramp") == 0) {
        gaussian_model_params.r_amp = strtof(val_str, NULL);
        printf("R amplitude: %.3f\r\n", (double)gaussian_model_params.r_amp);
    } else if (strcmp(param, "samp") == 0) {
        gaussian_model_params.s_amp = strtof(val_str, NULL);
        printf("S amplitude: %.3f\r\n", (double)gaussian_model_params.s_amp);
    } else if (strcmp(param, "tamp") == 0) {
        gaussian_model_params.t_amp = strtof(val_str, NULL);
        printf("T amplitude: %.3f\r\n", (double)gaussian_model_params.t_amp);
    } else if (strcmp(param, "tmu") == 0) {
        gaussian_model_params.t_mu_ms = strtof(val_str, NULL);
        printf("T offset: %.1f ms\r\n", (double)gaussian_model_params.t_mu_ms);
    } else if (strcmp(param, "tsigma") == 0) {
        gaussian_model_params.t_sigma_ms = strtof(val_str, NULL);
        printf("T width: %.1f ms\r\n", (double)gaussian_model_params.t_sigma_ms);
    } else if (strcmp(param, "pep") == 0) {
        gaussian_model_params.pep_ms = strtof(val_str, NULL);
        printf("PEP: %.1f ms\r\n", (double)gaussian_model_params.pep_ms);
    } else if (strcmp(param, "len") == 0) {
        gaussian_model_params.path_length_m = strtof(val_str, NULL);
        printf("Path length: %.3f m\r\n", (double)gaussian_model_params.path_length_m);
    } else if (strcmp(param, "pwv") == 0) {
        gaussian_model_params.pwv_mps = strtof(val_str, NULL);
        printf("PWV: %.3f m/s\r\n", (double)gaussian_model_params.pwv_mps);
    } else if (strcmp(param, "ectamp") == 0) {
        gaussian_model_params.ectopic_ppg_amp_scale = strtof(val_str, NULL);
        printf("Ectopic PPG amp scale: %.3f\r\n", (double)gaussian_model_params.ectopic_ppg_amp_scale);
    } else if (strcmp(param, "ectwiden") == 0) {
        gaussian_model_params.ectopic_qrs_widen = strtof(val_str, NULL);
        printf("Ectopic QRS widen: %.3f\r\n", (double)gaussian_model_params.ectopic_qrs_widen);
    } else if (strcmp(param, "ectdelay") == 0) {
        gaussian_model_params.ectopic_qrs_delay_ms = strtof(val_str, NULL);
        printf("Ectopic QRS delay: %.1f ms\r\n", (double)gaussian_model_params.ectopic_qrs_delay_ms);
    } else if (strcmp(param, "ectpep") == 0) {
        gaussian_model_params.ectopic_pep_ms = strtof(val_str, NULL);
        printf("Ectopic extra PEP: %.1f ms\r\n", (double)gaussian_model_params.ectopic_pep_ms);
    } else if (strcmp(param, "ppgamp") == 0) {
        int val = atoi(val_str);
        if (val < 0 || val > 100) {
            printf("Invalid ppgamp. Range: 0-100\r\n");
        } else {
            signal_gen_set_ppg_scale((float)val / 100.0f);
            printf("PPG amplitude: %d%%\r\n", val);
        }
    } else if (strcmp(param, "ppgtau") == 0 || strcmp(param, "ppggain") == 0 || strcmp(param, "ppgoff") == 0 || strcmp(param, "ppgd0") == 0 || strcmp(param, "ppgpre") == 0) {
        printf("PPG preemphasis removed\r\n");
    } else if (strcmp(param, "buzzer") == 0) {
        settings.buzzer_enabled = (strcmp(val_str, "on") == 0);
        printf("Buzzer: %s\r\n", settings.buzzer_enabled ? "ON" : "OFF");
    } else if (strcmp(param, "led") == 0) {
        settings.led_enabled = (strcmp(val_str, "on") == 0);
        printf("LED: %s\r\n", settings.led_enabled ? "ON" : "OFF");
    } else if (strcmp(param, "cal") == 0) {
        signal_gen_set_cal_gain(strtof(val_str, NULL));
        printf("Cal gain: %.3f (counts per mV at 100%% amp)\r\n", (double)signal_gen_get_cal_gain());
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
    printf("  rhythm    = %d (%s)\r\n", settings.rhythm_index, gaussian_model_get_rhythm_name(settings.rhythm_index));
    printf("  PR        = %.1f ms (Mobitz +%.1f ms)\r\n", (double)gaussian_model_params.pr_interval_ms, (double)gaussian_model_params.pr_increment_ms);
    printf("  escape    = %.1f bpm\r\n", (double)gaussian_model_params.escape_rate_bpm);
    printf("  buzzer    = %s\r\n", settings.buzzer_enabled ? "on" : "off");
    printf("  led       = %s\r\n", settings.led_enabled ? "on" : "off");
    printf("  cal       = %.3f (mV->counts multiplier)\r\n", (double)signal_gen_get_cal_gain());
    fflush(stdout);
}

static void cli_cmd_list(const char* args) {
    (void)args;
    uint8_t count = gaussian_model_get_rhythm_count();
    printf("Rhythm presets (%d):\r\n", count);
    for (uint8_t i = 0; i < count; i++) {
        printf("  [%d] %s\r\n", i, gaussian_model_get_rhythm_name(i));
    }
    fflush(stdout);
}

static void cli_cmd_rhythm(const char* args) {
    // Shortcut: `> rhythm <idx>` behaves like `> set rhythm <idx>`.
    if (!args || *args == '\0') {
        printf("Usage: > rhythm <index>. Type '> list' for presets.\r\n");
        fflush(stdout);
        return;
    }
    char work[CLI_BUF_SIZE];
    strncpy(work, args, CLI_BUF_SIZE - 1);
    work[CLI_BUF_SIZE - 1] = '\0';
    cli_cmd_set(skip_whitespace(work));
}

static void cli_cmd_defaults(const char* args) {
    (void)args;
    gaussian_model_reset_defaults();
    settings.rhythm_index = GAUSSIAN_RHYTHM_NORMAL_SINUS;
    settings.heart_rate = (uint16_t)gaussian_model_params.atrial_rate_bpm;
    signal_gen_set_bpm(settings.heart_rate);
    printf("Parameters restored to factory boot config: %s (%.0f bpm, PR %.0f ms)\r\n",
           gaussian_model_get_rhythm_name(GAUSSIAN_RHYTHM_NORMAL_SINUS),
           (double)gaussian_model_params.atrial_rate_bpm,
           (double)gaussian_model_params.pr_interval_ms);
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

static void cli_cmd_status(const char* args) {
    (void)args;
    printf("Status: OK\r\n");
    fflush(stdout);
}

static void cli_cmd_ver(const char* args) {
    (void)args;
    printf("03_pico_ecg_gaussian_ppg v1.2 (2026)\r\n");
    printf("Board: RP2350 (Pico 2)\r\n");
    printf("Model: Huynh, Tran & Tran, \"An ECG-PPG Physiological Signal\r\n");
    printf("Emulator...\", Sensors 2026, 26, 5578\r\n");
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
    printf("PROJECT=03_pico_ecg_gaussian_ppg\r\n");
    printf("VERSION=1.2\r\n");
    printf("BOARD=RP2350 (Pico 2)\r\n");
    printf("MODEL=Huynh/Tran 2026 Gaussian ECG + 5-Gaussian PPG\r\n");
    printf("LEADS=9 ECG (RA-referenced) + 1 PPG (GPIO 10)\r\n");
    printf("CHANNELS=RA_ref,I,II,V1,V2,V3,V4,V5,V6,PPG\r\n");
    printf("SAMPLE_RATE=2000\r\n");
    printf("PARAMS=hr,amp,offset,rhythm,pr,princ,escape,respdepth,respfreq,qtexp,pamp,pmu,psigma,qamp,ramp,samp,tamp,tmu,tsigma,pep,len,pwv,ectamp,ectwiden,ectdelay,ectpep,ppgamp\r\n");
    printf("SIGNAL_TYPES=ecg,sine,tri,sq,pulse\r\n");
    fflush(stdout);
}

