#include "cli.h"
#include "streaming.h"
#include "menu.h"
#include "signal_gen.h"
#include "signal_registry.h"
#include "ecg_types.h"
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

// ==================== Command Table ====================
typedef struct {
    const char* name;
    const char* help;
    void (*handler)(const char* args);
} cli_command_t;

static const cli_command_t commands[] = {
    { "help",   "Show available commands",           cli_cmd_help   },
    { "set",    "Set a parameter (type/hr/amp/...)", cli_cmd_set    },
    { "get",    "Show current settings",             cli_cmd_get    },
    { "list",   "List available ECG signals",        cli_cmd_list   },
    { "play",   "Start signal generation",           cli_cmd_play   },
    { "stop",   "Stop signal generation",            cli_cmd_stop   },
    { "stream", "Stream data over USB CDC (on/off)", cli_cmd_stream },
    { "ver",    "Show firmware version",             cli_cmd_ver    },
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
    // Detect '>' prefix to start command collection
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
    printf("Available commands:\r\n");
    for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
        printf("  %-8s %s\r\n", commands[i].name, commands[i].help);
    }
    printf("\r\nSet params: type, hr, amp, offset, file, buzzer, led\r\n");
    printf("Signal types: ecg, sine, tri, sq, pulse\r\n");
    fflush(stdout);
}

static void cli_cmd_set(const char* args) {
    if (!args || *args == '\0') {
        printf("Usage: > set <param> <value>\r\n");
        printf("Params: type, hr, amp, offset, file, buzzer, led\r\n");
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
            const ecg_struct_t* sig = signal_registry_get_by_index(0);
            if (sig) signal_gen_set_signal(sig);
            signal_gen_set_amplitude(settings.amplitude * 240);
            settings.ecg_type = 0;
            printf("Signal type: ECG\r\n");
        } else if (strcmp(val_str, "sine") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_SINE);
            signal_gen_set_bpm(settings.heart_rate);
            signal_gen_set_amplitude(settings.amplitude * 327);
            settings.ecg_type = 1;
            printf("Signal type: Sine\r\n");
        } else if (strcmp(val_str, "tri") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_TRIANGLE);
            signal_gen_set_bpm(settings.heart_rate);
            signal_gen_set_amplitude(settings.amplitude * 327);
            settings.ecg_type = 2;
            printf("Signal type: Triangle\r\n");
        } else if (strcmp(val_str, "sq") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_SQUARE);
            signal_gen_set_bpm(settings.heart_rate);
            signal_gen_set_amplitude(settings.amplitude * 327);
            settings.ecg_type = 3;
            printf("Signal type: Square\r\n");
        } else if (strcmp(val_str, "pulse") == 0) {
            signal_gen_set_signal_type(SIGNAL_TYPE_PULSE);
            signal_gen_set_bpm(settings.heart_rate);
            signal_gen_set_amplitude(settings.amplitude * 327);
            settings.ecg_type = 4;
            printf("Signal type: Pulse\r\n");
        } else {
            printf("Unknown type: '%s'. Use: ecg, sine, tri, sq, pulse\r\n", val_str);
        }
    } else if (strcmp(param, "hr") == 0) {
        int val = atoi(val_str);
        if (val < 1 || val > 3000) {
            printf("Invalid heart rate. Range: 1-3000\r\n");
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
            if (type == SIGNAL_TYPE_ECG) {
                signal_gen_set_amplitude(settings.amplitude * 240);
            } else {
                signal_gen_set_amplitude(settings.amplitude * 327);
            }
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
    } else if (strcmp(param, "file") == 0) {
        int val = atoi(val_str);
        uint8_t count = signal_registry_get_count();
        if (val < 0 || val >= count) {
            printf("Invalid file index. Range: 0-%d\r\n", count - 1);
        } else {
            settings.selected_file = (uint8_t)val;
            const ecg_struct_t* sig = signal_registry_get_by_index(settings.selected_file);
            if (sig) {
                signal_gen_set_signal(sig);
                signal_gen_set_signal_type(SIGNAL_TYPE_ECG);
                settings.ecg_type = 0;
                printf("Loaded: %s\r\n", signal_registry_get_filename(settings.selected_file));
                printf("  Rhythms: %s\r\n", sig->rhythms);
            }
        }
    } else if (strcmp(param, "buzzer") == 0) {
        if (strcmp(val_str, "on") == 0) {
            settings.buzzer_enabled = true;
            printf("Buzzer: ON\r\n");
        } else if (strcmp(val_str, "off") == 0) {
            settings.buzzer_enabled = false;
            printf("Buzzer: OFF\r\n");
        } else {
            printf("Usage: > set buzzer <on|off>\r\n");
        }
    } else if (strcmp(param, "led") == 0) {
        if (strcmp(val_str, "on") == 0) {
            settings.led_enabled = true;
            printf("LED: ON\r\n");
        } else if (strcmp(val_str, "off") == 0) {
            settings.led_enabled = false;
            printf("LED: OFF\r\n");
        } else {
            printf("Usage: > set led <on|off>\r\n");
        }
    } else {
        printf("Unknown parameter: '%s'\r\n", param);
        printf("Params: type, hr, amp, offset, file, buzzer, led\r\n");
    }
    fflush(stdout);
}

static void cli_cmd_get(const char* args) {
    if (!args || *args == '\0') {
        const char* type_str = "?";
        signal_type_t t = signal_gen_get_signal_type();
        switch (t) {
            case SIGNAL_TYPE_ECG:      type_str = "ecg";      break;
            case SIGNAL_TYPE_SINE:     type_str = "sine";     break;
            case SIGNAL_TYPE_TRIANGLE: type_str = "tri";      break;
            case SIGNAL_TYPE_SQUARE:   type_str = "sq";       break;
            case SIGNAL_TYPE_PULSE:    type_str = "pulse";    break;
        }

        printf("Settings:\r\n");
        printf("  type      = %s\r\n", type_str);
        printf("  hr        = %u bpm\r\n", settings.heart_rate);
        printf("  amp       = %u%%\r\n", settings.amplitude);
        printf("  offset    = %d\r\n", settings.offset);
        printf("  file      = %d (%s)\r\n", settings.selected_file,
               signal_registry_get_filename(settings.selected_file));
        printf("  buzzer    = %s\r\n", settings.buzzer_enabled ? "on" : "off");
        printf("  led       = %s\r\n", settings.led_enabled ? "on" : "off");
        printf("  playing   = %s\r\n", settings.is_playing ? "yes" : "no");
        fflush(stdout);
        return;
    }

    char work[CLI_BUF_SIZE];
    strncpy(work, args, CLI_BUF_SIZE - 1);
    work[CLI_BUF_SIZE - 1] = '\0';
    char* param = skip_whitespace(work);

    if (strcmp(param, "type") == 0) {
        signal_type_t t = signal_gen_get_signal_type();
        const char* s = "?";
        switch (t) {
            case SIGNAL_TYPE_ECG: s = "ecg"; break;
            case SIGNAL_TYPE_SINE: s = "sine"; break;
            case SIGNAL_TYPE_TRIANGLE: s = "tri"; break;
            case SIGNAL_TYPE_SQUARE: s = "sq"; break;
            case SIGNAL_TYPE_PULSE: s = "pulse"; break;
        }
        printf("type = %s\r\n", s);
    } else if (strcmp(param, "hr") == 0) {
        printf("hr = %u bpm\r\n", settings.heart_rate);
    } else if (strcmp(param, "amp") == 0) {
        printf("amp = %u%%\r\n", settings.amplitude);
    } else if (strcmp(param, "offset") == 0) {
        printf("offset = %d\r\n", settings.offset);
    } else if (strcmp(param, "file") == 0) {
        printf("file = %d (%s)\r\n", settings.selected_file,
               signal_registry_get_filename(settings.selected_file));
    } else if (strcmp(param, "buzzer") == 0) {
        printf("buzzer = %s\r\n", settings.buzzer_enabled ? "on" : "off");
    } else if (strcmp(param, "led") == 0) {
        printf("led = %s\r\n", settings.led_enabled ? "on" : "off");
    } else {
        printf("Unknown param: '%s'\r\n", param);
    }
    fflush(stdout);
}

static void cli_cmd_list(const char* args) {
    uint8_t count = signal_registry_get_count();
    printf("ECG Signals (%d):\r\n", count);
    for (uint8_t i = 0; i < count; i++) {
        const ecg_struct_t* sig = signal_registry_get_by_index(i);
        const char* name = signal_registry_get_filename(i);
        printf("  [%2d] %-20s", i, name);
        if (sig) {
            printf("  %dx%d", sig->rows, sig->cols);
        }
        printf("\r\n");
    }
    fflush(stdout);
}

static void cli_cmd_play(const char* args) {
    signal_gen_start();
    settings.is_playing = true;
    printf("Signal generation: STARTED\r\n");
    fflush(stdout);
}

static void cli_cmd_stop(const char* args) {
    signal_gen_stop();
    settings.is_playing = false;
    printf("Signal generation: STOPPED\r\n");
    fflush(stdout);
}

static void cli_cmd_ver(const char* args) {
    printf("pico_ecg v1.1 (2026) - ECG Monitor\r\n");
    printf("Board: RP2350 (Pico 2)\r\n");
    printf("CLI: v1.1\r\n");
    fflush(stdout);
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

bool cli_is_streaming(void) {
    return stream_mode_active;
}

void cli_set_streaming(bool on) {
    stream_mode_active = on;
    if (on) {
        stream_buffer.write_idx = 0;
        stream_buffer.read_idx  = 0;
    }
}

// ==================== Start Collecting (called from terminal_control) ====================
void cli_start_collecting(void) {
    collecting = true;
    cmd_len = 0;
}
