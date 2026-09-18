#include "terminal_control.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include "menu.h"
#include "gem_core.h"
#include "signal_registry.h"
#include "cli.h"
#include "streaming.h"
#include <string.h>

static enum {
    STATE_NORMAL,
    STATE_ESC,
    STATE_BRACKET
} parse_state = STATE_NORMAL;

static uint8_t last_cursor = 255;
static uint8_t last_scroll = 255;
static const gem_page_t* last_page = NULL;
static bool last_editing = false;

/**
 * @brief Prints the current menu status to the terminal.
 */
static void terminal_control_print_menu(void) {
    gem_page_t* page = menu_gem.current_page;
    if (!page) return;

    // ANSI: Clear screen and home cursor
    printf("\033[H\033[2J");
    printf("\r\n--- TERMINAL MENU: %s ---\r\n", page->title);

    // Specific logic for File Info page
    if (menu_is_on_file_info_page() && settings.selected_file < signal_registry_get_count()) {
        const ecg_struct_t* signal = signal_registry_get_by_index(settings.selected_file);
        if (signal != NULL) {
            printf("File: %s\r\n", signal_registry_get_filename(settings.selected_file));
            printf("Rhythms: %s\r\n", signal->rhythms);
            printf("Condition: %s\r\n", signal->cond_abnor);
            printf("Hypertrophies: %s\r\n", signal->hypertrophies);
            printf("Ischemia: %s\r\n", signal->ischemia);
            printf("Pacing: %s\r\n", signal->pacing);
            printf("Extrasystolies: %s\r\n", signal->extrasystolies);
            printf("Size: %d rows x %d cols\r\n", signal->rows, signal->cols);
            printf("\r\n(Arrows Up/Down to Scroll Pager on OLED)\r\n");
            printf("(Press Arrow Left or Esc to Return)\r\n");
            fflush(stdout);
            
            last_cursor = menu_gem.cursor_row;
            last_scroll = menu_gem.scroll_offset;
            last_page = page;
            return;
        }
    }
    
    int current_idx = menu_gem.scroll_offset + menu_gem.cursor_row;

    for (int i = 0; i < page->item_count; i++) {
        gem_item_t* item = page->items[i];
        bool selected = (i == current_idx);
        
        printf("%s %-15s", selected ? " >" : "  ", item->title);
        
        if (item->type == GEM_ITEM_VAL) {
            char val_str[16] = "";
            switch (item->linked_type) {
                case GEM_VAL_UINT8:   sprintf(val_str, ": %u", *(uint8_t*)item->linked_variable); break;
                case GEM_VAL_UINT16:  sprintf(val_str, ": %u", *(uint16_t*)item->linked_variable); break;
                case GEM_VAL_INT16:   sprintf(val_str, ": %d", *(int16_t*)item->linked_variable); break;
                case GEM_VAL_BOOL:    sprintf(val_str, ": %s", (*(bool*)item->linked_variable) ? "ON" : "OFF"); break;
                case GEM_VAL_FLOAT:   sprintf(val_str, ": %.1f", (double)*(float*)item->linked_variable); break;
                default: break;
            }
            if (selected && menu_gem.is_editing) {
                printf(" [%s] << EDITING", val_str + 2);
            } else {
                printf("%s", val_str);
            }
        } else if (item->type == GEM_ITEM_LINK) {
            printf(" >>");
        }
        printf("\r\n");
    }
    printf("\r\n(Arrows to navigate, Enter to select, Backspace/Esc for back)\r\n");
    printf("(Type '> ' prefix for CLI commands)\r\n");
    fflush(stdout);

    // Update last states
    last_cursor = menu_gem.cursor_row;
    last_scroll = menu_gem.scroll_offset;
    last_page = page;
    last_editing = menu_gem.is_editing;
}

void terminal_control_init(void) {
    cli_init();
    terminal_control_print_menu();
}

static void send_gem_key(gem_key_t key) {
    if (key != GEM_KEY_NONE) {
        gem_register_key(&menu_gem, key);
        // Force immediate refresh
        terminal_control_print_menu();
    }
}

void terminal_control_update(void) {
    // ---- STREAMING MODE ----
    // When active: drain ring buffer, send binary frames, still accept '> stream off'.
    if (cli_is_streaming()) {
        if (!cli_output_active()) {
            float sample[STREAM_NUM_CHANNELS];
            while (stream_buffer_read(sample)) {
                uint8_t header[2]  = {0xAA, 0x55};
                uint8_t trailer[2] = {0x55, 0xAA};
                fwrite(header,  1, 2,                    stdout);
                fwrite(sample,  sizeof(float), STREAM_NUM_CHANNELS, stdout);
                fwrite(trailer, 1, 2,                    stdout);
            }
            fflush(stdout);
        }
        // Still accept '> stream off' while streaming
        int c = getchar_timeout_us(0);
        while (c != PICO_ERROR_TIMEOUT) {
            if (cli_is_collecting()) {
                cli_feed_char(c);
            } else if (c == '>') {
                cli_feed_char(c);
            }
            c = getchar_timeout_us(0);
        }
        return;
    }

    // 1. Check if state changed externally (e.g. from encoder)
    //    Skip refresh if CLI just executed a command (cooldown)
    if (!cli_is_collecting() && !cli_output_active() &&
        (menu_gem.current_page != last_page || 
         menu_gem.cursor_row != last_cursor || 
         menu_gem.scroll_offset != last_scroll ||
         menu_gem.is_editing != last_editing)) {
        terminal_control_print_menu();
    }

    // 2. Read terminal input
    int c = getchar_timeout_us(0);
    while (c != PICO_ERROR_TIMEOUT) {
        if (cli_is_collecting()) {
            // CLI is collecting input: route all chars to CLI
            cli_feed_char(c);
        } else if (c == '>') {
            // Detect '>' prefix: start CLI command collection
            cli_feed_char(c);
        } else {
            // Normal GEM menu input handling
            switch (parse_state) {
                case STATE_NORMAL:
                    if (c == 0x1B) {
                        parse_state = STATE_ESC;
                    } else if (c == '\r' || c == '\n') {
                        send_gem_key(GEM_KEY_OK);
                    } else if (c == 0x7F || c == '\b') {
                        send_gem_key(GEM_KEY_CANCEL);
                    }
                    break;
                case STATE_ESC:
                    if (c == '[') {
                        parse_state = STATE_BRACKET;
                    } else if (c == 0x1B) {
                        send_gem_key(GEM_KEY_CANCEL);
                        parse_state = STATE_NORMAL;
                    } else {
                        parse_state = STATE_NORMAL;
                    }
                    break;
                case STATE_BRACKET:
                    if (c == 'A') send_gem_key(GEM_KEY_UP);
                    else if (c == 'B') send_gem_key(GEM_KEY_DOWN);
                    else if (c == 'C') send_gem_key(GEM_KEY_RIGHT);
                    else if (c == 'D') send_gem_key(GEM_KEY_LEFT);
                    parse_state = STATE_NORMAL;
                    break;
            }
        }
        c = getchar_timeout_us(0);
    }
}
