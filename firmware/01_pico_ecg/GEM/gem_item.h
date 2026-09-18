#ifndef GEM_ITEM_H
#define GEM_ITEM_H

#include "gem_common.h"

// Forward declaration
struct gem_page;

typedef struct gem_item {
    const char* title;
    gem_item_type_t type;
    
    // Linked variable info
    void* linked_variable;
    gem_val_type_t linked_type;
    bool readonly;
    bool hidden;

    // Callback
    gem_callback_t callback;
    gem_callback_data_t callback_data;

    // Navigation/Hierarchy
    struct gem_page* link_page; // For LINK items

    // Formatting
    int precision; // For floats

    // Internal state for editing (could be moved to core, but convenient here)
    // Actually, editing state is usually global or per-page, but let's keep it simple for now.

} gem_item_t;

// Constructors / Init functions
void gem_item_init_val_int(gem_item_t* item, const char* title, int* val, gem_callback_t callback);
void gem_item_init_val_uint8(gem_item_t* item, const char* title, uint8_t* val, gem_callback_t callback);
void gem_item_init_val_uint16(gem_item_t* item, const char* title, uint16_t* val, gem_callback_t callback);
void gem_item_init_val_int16(gem_item_t* item, const char* title, int16_t* val, gem_callback_t callback);
void gem_item_init_val_bool(gem_item_t* item, const char* title, bool* val, gem_callback_t callback);
void gem_item_init_val_float(gem_item_t* item, const char* title, float* val, gem_callback_t callback);
void gem_item_init_link(gem_item_t* item, const char* title, struct gem_page* page);
void gem_item_init_button(gem_item_t* item, const char* title, gem_callback_t callback);

// Setters / Getters - Basic
void gem_item_set_readonly(gem_item_t* item, bool readonly);
void gem_item_hide(gem_item_t* item, bool hidden);
bool gem_item_is_hidden(gem_item_t* item);

// Setters / Getters - Link (para submenús)
void gem_item_set_linked_page(gem_item_t* item, struct gem_page* page);
struct gem_page* gem_item_get_linked_page(gem_item_t* item);

// Getters - Variable and Callbacks
void* gem_item_get_linked_variable(gem_item_t* item);
gem_val_type_t gem_item_get_linked_type(gem_item_t* item);
gem_callback_t gem_item_get_callback(gem_item_t* item);
const char* gem_item_get_title(gem_item_t* item);
gem_item_type_t gem_item_get_type(gem_item_t* item);

// Label item initialization
void gem_item_init_label(gem_item_t* item, const char* title);

#endif // GEM_ITEM_H
