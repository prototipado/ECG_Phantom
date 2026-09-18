#include "gem_item.h"
#include <string.h>

static void base_init(gem_item_t* item, const char* title, gem_item_type_t type) {
    item->title = title;
    item->type = type;
    item->linked_variable = NULL;
    item->callback = NULL;
    item->readonly = false;
    item->hidden = false;
    item->precision = 2; // Default float precision
    item->link_page = NULL;
    // callback_data init to zero?
    memset(&item->callback_data, 0, sizeof(gem_callback_data_t));
    item->callback_data.pMenuItem = item;
}

void gem_item_init_val_int(gem_item_t* item, const char* title, int* val, gem_callback_t callback) {
    base_init(item, title, GEM_ITEM_VAL);
    item->linked_variable = val;
    item->linked_type = GEM_VAL_INTEGER;
    item->callback = callback;
}

void gem_item_init_val_uint8(gem_item_t* item, const char* title, uint8_t* val, gem_callback_t callback) {
    base_init(item, title, GEM_ITEM_VAL);
    item->linked_variable = val;
    item->linked_type = GEM_VAL_UINT8;
    item->callback = callback;
}

void gem_item_init_val_uint16(gem_item_t* item, const char* title, uint16_t* val, gem_callback_t callback) {
    base_init(item, title, GEM_ITEM_VAL);
    item->linked_variable = val;
    item->linked_type = GEM_VAL_UINT16;
    item->callback = callback;
}

void gem_item_init_val_int16(gem_item_t* item, const char* title, int16_t* val, gem_callback_t callback) {
    base_init(item, title, GEM_ITEM_VAL);
    item->linked_variable = val;
    item->linked_type = GEM_VAL_INT16;
    item->callback = callback;
}

void gem_item_init_val_bool(gem_item_t* item, const char* title, bool* val, gem_callback_t callback) {
    base_init(item, title, GEM_ITEM_VAL);
    item->linked_variable = val;
    item->linked_type = GEM_VAL_BOOL;
    item->callback = callback;
}

void gem_item_init_val_float(gem_item_t* item, const char* title, float* val, gem_callback_t callback) {
    base_init(item, title, GEM_ITEM_VAL);
    item->linked_variable = val;
    item->linked_type = GEM_VAL_FLOAT;
    item->callback = callback;
}

void gem_item_init_link(gem_item_t* item, const char* title, struct gem_page* page) {
    base_init(item, title, GEM_ITEM_LINK);
    item->link_page = page;
}

void gem_item_init_button(gem_item_t* item, const char* title, gem_callback_t callback) {
    base_init(item, title, GEM_ITEM_BUTTON);
    item->callback = callback;
}

void gem_item_set_readonly(gem_item_t* item, bool readonly) {
    item->readonly = readonly;
}

void gem_item_hide(gem_item_t* item, bool hidden) {
    item->hidden = hidden;
}

bool gem_item_is_hidden(gem_item_t* item) {
    return item->hidden;
}

// ===== SETTERS / GETTERS PARA LINK ITEMS (SUBMENÚS) =====

void gem_item_set_linked_page(gem_item_t* item, struct gem_page* page) {
    item->type = GEM_ITEM_LINK;
    item->link_page = page;
}

struct gem_page* gem_item_get_linked_page(gem_item_t* item) {
    if (item->type == GEM_ITEM_LINK) {
        return item->link_page;
    }
    return NULL;
}

// ===== GETTERS GENERALES =====

void* gem_item_get_linked_variable(gem_item_t* item) {
    return item->linked_variable;
}

gem_val_type_t gem_item_get_linked_type(gem_item_t* item) {
    return item->linked_type;
}

gem_callback_t gem_item_get_callback(gem_item_t* item) {
    return item->callback;
}

const char* gem_item_get_title(gem_item_t* item) {
    return item->title;
}

gem_item_type_t gem_item_get_type(gem_item_t* item) {
    return item->type;
}

// ===== INICIALIZADOR PARA ITEMS LABEL =====

void gem_item_init_label(gem_item_t* item, const char* title) {
    base_init(item, title, GEM_ITEM_LABEL);
}
