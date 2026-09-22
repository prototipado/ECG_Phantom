#ifndef GEM_CORE_H
#define GEM_CORE_H

#include "gem_common.h"
#include "gem_hal.h"
#include "gem_page.h"

// Menu pointer appearance
typedef enum {
    GEM_POINTER_ROW,
    GEM_POINTER_DASH
} gem_pointer_type_t;

typedef struct {
    gem_hal_t hal;
    gem_page_t* current_page;
    
    // Appearance settings
    gem_pointer_type_t pointer_type;
    uint8_t rows_per_screen;
    uint8_t row_height;
    uint8_t top_offset;
    uint8_t val_left_offset;
    
    // State
    uint8_t cursor_row; // Row index on screen (0 to rows_per_screen-1)
    uint8_t scroll_offset; // Index of the top-most visible item
    
    // Edit state
    bool is_editing;
    gem_item_t* editing_item;
    // ... buffer for editing values ...

} gem_t;

// Init
void gem_init(gem_t* gem, gem_hal_t hal, gem_page_t* initial_page);

// ===== PAGE NAVIGATION =====
void gem_set_current_page(gem_t* gem, gem_page_t* page);
gem_page_t* gem_get_current_page(gem_t* gem);

// ===== APPEARANCE SETTINGS =====
void gem_set_appearance(gem_t* gem, gem_pointer_type_t pointer_type,
                        uint8_t rows_per_screen, uint8_t row_height,
                        uint8_t top_offset, uint8_t val_left_offset);

// ===== STATE GETTERS =====
bool gem_is_editing(gem_t* gem);
gem_item_t* gem_get_editing_item(gem_t* gem);
uint8_t gem_get_cursor_row(gem_t* gem);
uint8_t gem_get_scroll_offset(gem_t* gem);

// Loop / Input
void gem_register_key(gem_t* gem, gem_key_t key);

// Draw
void gem_draw(gem_t* gem);

#endif // GEM_CORE_H
