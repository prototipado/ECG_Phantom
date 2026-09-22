#ifndef GEM_HAL_H
#define GEM_HAL_H

#include "gem_common.h"

// Font info structure
typedef struct {
    uint8_t width;
    uint8_t height;
} gem_font_info_t;

// HAL Interface - User must implement these
typedef struct {
    // Drawing
    void (*draw_pixel)(int x, int y, int color);
    void (*draw_rect)(int x, int y, int w, int h, int color); // Outline
    void (*draw_flied_rect)(int x, int y, int w, int h, int color); // Fill
    void (*draw_text)(int x, int y, const char* text);
    
    // Screen control
    void (*clear_screen)(void);
    void (*update_screen)(void); // Optional, for buffered displays

    // Font metrics
    gem_font_info_t (*get_font_info)(void);
    int (*get_text_width)(const char* text);

    // System
    uint32_t (*millis)(void);

    // Screen dimensions
    int screen_width;
    int screen_height;

} gem_hal_t;

#endif // GEM_HAL_H
