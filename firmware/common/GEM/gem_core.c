#include "gem_core.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

void gem_init(gem_t* gem, gem_hal_t hal, gem_page_t* initial_page) {
    gem->hal = hal;
    gem->current_page = initial_page;
    
    // Default appearance
    gem->pointer_type = GEM_POINTER_ROW;
    gem->rows_per_screen = 4; // 4 items per screen (rest is title and scrolling space)
    gem->row_height = 10;
    gem->top_offset = 10;
    gem->val_left_offset = 80;

    gem->cursor_row = 0;
    gem->scroll_offset = 0;
    gem->is_editing = false;
    gem->editing_item = NULL;
    
    if (initial_page != NULL) {
        gem_page_set_current_item_index(initial_page, 0);
    }
}

// ===== PAGE NAVIGATION =====

void gem_set_current_page(gem_t* gem, gem_page_t* page) {
    if (gem != NULL && page != NULL) {
        gem->current_page = page;
        gem->cursor_row = 0;
        gem->scroll_offset = 0;
        gem->is_editing = false;
        gem_page_set_current_item_index(page, 0);
    }
}

gem_page_t* gem_get_current_page(gem_t* gem) {
    return (gem != NULL) ? gem->current_page : NULL;
}

// ===== APPEARANCE SETTINGS =====

void gem_set_appearance(gem_t* gem, gem_pointer_type_t pointer_type,
                        uint8_t rows_per_screen, uint8_t row_height,
                        uint8_t top_offset, uint8_t val_left_offset) {
    if (gem == NULL) return;
    
    gem->pointer_type = pointer_type;
    gem->rows_per_screen = rows_per_screen;
    gem->row_height = row_height;
    gem->top_offset = top_offset;
    gem->val_left_offset = val_left_offset;
}

static void draw_item_row(gem_t* gem, int row_idx, gem_item_t* item, bool selected) {
    int y = gem->top_offset + (row_idx * gem->row_height);
    
    // Draw background if selected (GEM_POINTER_ROW style)
    if (selected && gem->pointer_type == GEM_POINTER_ROW) {
        // Invert colors usually, or specific background
        // Simplification: Just draw generic selection rect?
        // Let's assume HAL handles color inversion or we just draw a box.
        // gem->hal.draw_flied_rect(0, y, gem->hal.screen_width, gem->row_height, 1);
        // For now, let's just print a > marker if not row style, or inverted text.
    }

    // Pointer
    if (selected) {
        if (gem->is_editing && gem->editing_item == item) {
            gem->hal.draw_text(0, y, "*"); // Asterisk for editing mode
        } else {
            gem->hal.draw_text(0, y, ">");
        }
    }

    // Title
    gem->hal.draw_text(10, y, item->title);

    // Value
    if (item->type == GEM_ITEM_VAL) {
        char val_str[32];
        switch (item->linked_type) {
            case GEM_VAL_INTEGER:
                sprintf(val_str, "%d", *(int*)item->linked_variable);
                break;
            case GEM_VAL_UINT8:
                sprintf(val_str, "%u", *(uint8_t*)item->linked_variable);
                break;
            case GEM_VAL_UINT16:
                sprintf(val_str, "%u", *(uint16_t*)item->linked_variable);
                break;
            case GEM_VAL_INT16:
                sprintf(val_str, "%d", *(int16_t*)item->linked_variable);
                break;
            case GEM_VAL_BOOL:
                sprintf(val_str, "%s", (*(bool*)item->linked_variable) ? "ON" : "OFF");
                break;
            case GEM_VAL_FLOAT:
                sprintf(val_str, "%.2f", *(float*)item->linked_variable);
                break;
            default:
                strcpy(val_str, "?");
        }
        gem->hal.draw_text(gem->val_left_offset, y, val_str);
    } else if (item->type == GEM_ITEM_LINK) {
        gem->hal.draw_text(gem->hal.screen_width - 10, y, ">>");
    }
}

void gem_draw(gem_t* gem) {
    gem->hal.clear_screen();
    
    // Draw Title
    int title_y = gem->top_offset - gem->row_height;
    if (title_y < 0) title_y = 0;
    
    gem->hal.draw_text(0, title_y, gem->current_page->title);
    gem->hal.draw_rect(0, title_y, gem->hal.screen_width, gem->row_height, 1); // Separator

    int items_to_draw = gem->rows_per_screen;
    if (gem->current_page->item_count < items_to_draw) {
        items_to_draw = gem->current_page->item_count;
    }

    for (int i = 0; i < items_to_draw; i++) {
        int item_idx = gem->scroll_offset + i;
        if (item_idx >= gem->current_page->item_count) break;
        
        gem_item_t* item = gem->current_page->items[item_idx];
        bool is_selected = (i == gem->cursor_row);
        draw_item_row(gem, i, item, is_selected);
    }
    
    if (gem->hal.update_screen) {
        gem->hal.update_screen();
    }
}

static void navigate_up(gem_t* gem) {
    int current_item_idx = gem->scroll_offset + gem->cursor_row;
    
    if (current_item_idx > 0) {
        // Not on first item
        if (gem->cursor_row > 0) {
            // Can move cursor up
            gem->cursor_row--;
        } else {
            // Cursor at top of visible area, scroll up
            gem->scroll_offset--;
        }
    }
}

static void navigate_down(gem_t* gem) {
    int current_item_idx = gem->scroll_offset + gem->cursor_row;
    int total_items = gem->current_page->item_count;
    
    /*
    printf("navigate_down: current_idx=%d, total=%d, cursor=%d, scroll=%d\n", 
           current_item_idx, total_items, gem->cursor_row, gem->scroll_offset);
    */
    
    if (current_item_idx < total_items - 1) {
        // Not on last item
        if (gem->cursor_row < gem->rows_per_screen - 1) {
            // Can move cursor down within visible area
            gem->cursor_row++;
        } else {
            // Cursor at bottom of visible area, scroll down
            gem->scroll_offset++;
            // Keep cursor_row at bottom
        }
    }
    
    // Validate bounds
    if (gem->scroll_offset < 0) gem->scroll_offset = 0;
    if (gem->cursor_row < 0) gem->cursor_row = 0;
    if (gem->cursor_row >= gem->rows_per_screen) gem->cursor_row = gem->rows_per_screen - 1;
    if (gem->scroll_offset + gem->cursor_row >= total_items) {
        gem->scroll_offset = total_items - gem->rows_per_screen;
        if (gem->scroll_offset < 0) gem->scroll_offset = 0;
        gem->cursor_row = total_items - gem->scroll_offset - 1;
        if (gem->cursor_row < 0) gem->cursor_row = 0;
    }
    
    // printf("navigate_down after: cursor=%d, scroll=%d\n", gem->cursor_row, gem->scroll_offset);
}

static void enter_item(gem_t* gem) {
    int current_idx = gem->scroll_offset + gem->cursor_row;
    
    if (gem->current_page == NULL || current_idx >= gem->current_page->item_count) {
        return;
    }
    
    gem_item_t* item = gem->current_page->items[current_idx];

    // Manejar item LINK (SUBMENÚS)
    if (item->type == GEM_ITEM_LINK) {
        gem_page_t* linked_page = gem_item_get_linked_page(item);
        if (linked_page != NULL) {
            gem_set_current_page(gem, linked_page);
        }
    } 
    // Manejar items VAL (variables editables)
    else if (item->type == GEM_ITEM_VAL) {
        if (!item->readonly) {
            gem->is_editing = true;
            gem->editing_item = item;
            // Lógica de edición
        }
    } 
    // Manejar BUTTON items
    else if (item->type == GEM_ITEM_BUTTON) {
        gem_callback_t callback = gem_item_get_callback(item);
        if (callback != NULL) {
            gem_callback_data_t callback_data;
            callback_data.pMenuItem = item;
            callback(callback_data);
        }
    }
}

static void exit_current_level(gem_t* gem) {
    if (gem->current_page != NULL) {
        gem_page_t* parent_page = gem_page_get_parent(gem->current_page);
        if (parent_page != NULL) {
            gem_set_current_page(gem, parent_page);
        }
    }
}

void gem_register_key(gem_t* gem, gem_key_t key) {
    if (gem->is_editing && gem->editing_item != NULL) {
        // Handle edit mode - change values with UP/DOWN
        if (key == GEM_KEY_UP) {
            // Increment value
            void* var = gem->editing_item->linked_variable;
            switch (gem->editing_item->linked_type) {
                case GEM_VAL_INTEGER:
                    (*(int*)var)++;
                    break;
                case GEM_VAL_UINT8:
                    if (*(uint8_t*)var < 255) (*(uint8_t*)var)++;
                    break;
                case GEM_VAL_UINT16:
                    if (*(uint16_t*)var < 65535) (*(uint16_t*)var)++;
                    break;
                case GEM_VAL_INT16:
                    if (*(int16_t*)var < 32767) (*(int16_t*)var)++;
                    break;
                case GEM_VAL_BOOL:
                    (*(bool*)var) = !(*(bool*)var);
                    break;
                case GEM_VAL_FLOAT:
                    (*(float*)var) += 0.1f;
                    break;
                default:
                    break;
            }
            // Call callback if exists
            if (gem->editing_item->callback != NULL) {
                gem->editing_item->callback(gem->editing_item->callback_data);
            }
        } else if (key == GEM_KEY_DOWN) {
            // Decrement value
            void* var = gem->editing_item->linked_variable;
            switch (gem->editing_item->linked_type) {
                case GEM_VAL_INTEGER:
                    (*(int*)var)--;
                    break;
                case GEM_VAL_UINT8:
                    if (*(uint8_t*)var > 0) (*(uint8_t*)var)--;
                    break;
                case GEM_VAL_UINT16:
                    if (*(uint16_t*)var > 0) (*(uint16_t*)var)--;
                    break;
                case GEM_VAL_INT16:
                    if (*(int16_t*)var > -32768) (*(int16_t*)var)--;
                    break;
                case GEM_VAL_BOOL:
                    (*(bool*)var) = !(*(bool*)var);
                    break;
                case GEM_VAL_FLOAT:
                    (*(float*)var) -= 0.1f;
                    break;
                default:
                    break;
            }
            // Call callback if exists
            if (gem->editing_item->callback != NULL) {
                gem->editing_item->callback(gem->editing_item->callback_data);
            }
        } else if (key == GEM_KEY_OK) {
            // Exit edit mode - save value
            gem->is_editing = false;
        } else if (key == GEM_KEY_CANCEL) {
            // Exit edit mode - abandon
            gem->is_editing = false;
        }
        return;
    }

    switch (key) {
        case GEM_KEY_UP:
            navigate_up(gem);
            break;
        case GEM_KEY_DOWN:
            navigate_down(gem);
            break;
        case GEM_KEY_OK:
        case GEM_KEY_RIGHT: // Right also enters?
            enter_item(gem);
            break;
        case GEM_KEY_LEFT:
        case GEM_KEY_CANCEL:
            exit_current_level(gem);
            break;
        default:
            break;
    }
}
// ===== STATE GETTERS =====

bool gem_is_editing(gem_t* gem) {
    return (gem != NULL) ? gem->is_editing : false;
}

gem_item_t* gem_get_editing_item(gem_t* gem) {
    return (gem != NULL) ? gem->editing_item : NULL;
}

uint8_t gem_get_cursor_row(gem_t* gem) {
    return (gem != NULL) ? gem->cursor_row : 0;
}

uint8_t gem_get_scroll_offset(gem_t* gem) {
    return (gem != NULL) ? gem->scroll_offset : 0;
}