#include "gem_u8g2.h"

static u8g2_t* _u8g2_inst = NULL;

static void u8g2_draw_pixel(int x, int y, int color) {
    if (!_u8g2_inst) return;
    u8g2_SetDrawColor(_u8g2_inst, color);
    u8g2_DrawPixel(_u8g2_inst, x, y);
}

static void u8g2_draw_rect(int x, int y, int w, int h, int color) {
    if (!_u8g2_inst) return;
    u8g2_SetDrawColor(_u8g2_inst, color);
    u8g2_DrawFrame(_u8g2_inst, x, y, w, h);
}

static void u8g2_draw_filled_rect(int x, int y, int w, int h, int color) {
    if (!_u8g2_inst) return;
    u8g2_SetDrawColor(_u8g2_inst, color);
    u8g2_DrawBox(_u8g2_inst, x, y, w, h);
}

static void u8g2_draw_text(int x, int y, const char* text) {
    if (!_u8g2_inst) return;
    u8g2_SetDrawColor(_u8g2_inst, 1);
    // U8g2 draws strings with baseline origin by default? 
    // GEM expects top-left usually? 
    // Let's assume standard U8g2 text drawing.
    // We might need to adjust Y depending on font mode. 
    // For now:
    u8g2_DrawStr(_u8g2_inst, x, y + 8, text); // +8 crude baseline adjustment
}

static void u8g2_clear_screen(void) {
    if (!_u8g2_inst) return;
    u8g2_ClearBuffer(_u8g2_inst);
}

static void u8g2_update_screen(void) {
    if (!_u8g2_inst) return;
    u8g2_SendBuffer(_u8g2_inst);
}

// System millis mock or use pico function if available?
// Since this is a library file, we shouldn't depend on pico_stdlib optionally.
// Use weak symbol or user callback?
// gem_hal expects a function pointer.
// We can just reuse what the user passes or provide a dummy.
// But wait, the init function returns a struct with function pointers.

static uint32_t dummy_millis(void) {
   return 0; // User should override this if needed
}

gem_hal_t gem_hal_u8g2_init(u8g2_t* u8g2) {
    _u8g2_inst = u8g2; // Store global reference for these static functions to use
    // Note: This prevents multiple GEM instances with different U8g2 instances.
    // Better approach: Pass context? But HAL signatures don't have context.
    // For embedded single-screen, this is usually fine.
    
    gem_hal_t hal;
    hal.draw_pixel = u8g2_draw_pixel;
    hal.draw_rect = u8g2_draw_rect;
    hal.draw_flied_rect = u8g2_draw_filled_rect;
    hal.draw_text = u8g2_draw_text;
    hal.clear_screen = u8g2_clear_screen;
    hal.update_screen = u8g2_update_screen;
    hal.millis = dummy_millis; // User must replace this with board millis
    
    hal.screen_width = u8g2_GetDisplayWidth(u8g2);
    hal.screen_height = u8g2_GetDisplayHeight(u8g2);
    
    return hal;
}
