#include "gem_page.h"
#include <stddef.h>
#include <string.h>

void gem_page_init(gem_page_t* page, const char* title, struct gem_page* parent) {
    page->title = title;
    page->parent_page = parent;
    page->item_count = 0;
    page->current_item_index = 0;
    for (int i = 0; i < GEM_MAX_ITEMS_PER_PAGE; i++) {
        page->items[i] = NULL;
    }
}

void gem_page_add_item(gem_page_t* page, gem_item_t* item) {
    if (page->item_count < GEM_MAX_ITEMS_PER_PAGE) {
        page->items[page->item_count++] = item;
    }
}

// ===== SETTERS / GETTERS PARA NAVEGACIÓN MULTINIVEL =====

void gem_page_set_parent(gem_page_t* page, struct gem_page* parent) {
    page->parent_page = parent;
}

struct gem_page* gem_page_get_parent(gem_page_t* page) {
    return page->parent_page;
}

// ===== GETTERS GENERALES =====

const char* gem_page_get_title(gem_page_t* page) {
    return page->title;
}

void gem_page_set_title(gem_page_t* page, const char* title) {
    page->title = title;
}

uint8_t gem_page_get_item_count(gem_page_t* page) {
    return page->item_count;
}

gem_item_t* gem_page_get_item(gem_page_t* page, uint8_t index) {
    if (index < page->item_count) {
        return page->items[index];
    }
    return NULL;
}

// ===== NAVEGACIÓN DE ITEMS EN LA PÁGINA =====

void gem_page_set_current_item_index(gem_page_t* page, uint8_t index) {
    if (index < page->item_count) {
        page->current_item_index = index;
    }
}

uint8_t gem_page_get_current_item_index(gem_page_t* page) {
    return page->current_item_index;
}

gem_item_t* gem_page_get_current_item(gem_page_t* page) {
    if (page->current_item_index < page->item_count) {
        return page->items[page->current_item_index];
    }
    return NULL;
}

// ===== UTILIDADES =====

void gem_page_remove_item(gem_page_t* page, gem_item_t* item) {
    int index = gem_page_get_item_index(page, item);
    if (index >= 0) {
        // Desplazar items hacia atrás
        for (int i = index; i < page->item_count - 1; i++) {
            page->items[i] = page->items[i + 1];
        }
        page->items[page->item_count - 1] = NULL;
        page->item_count--;
        
        // Ajustar índice actual si es necesario
        if (page->current_item_index >= page->item_count && page->item_count > 0) {
            page->current_item_index = page->item_count - 1;
        }
    }
}

gem_item_t* gem_page_find_item_by_title(gem_page_t* page, const char* title) {
    for (uint8_t i = 0; i < page->item_count; i++) {
        if (page->items[i] != NULL && 
            gem_item_get_title(page->items[i]) != NULL &&
            strcmp(gem_item_get_title(page->items[i]), title) == 0) {
            return page->items[i];
        }
    }
    return NULL;
}

int gem_page_get_item_index(gem_page_t* page, gem_item_t* item) {
    for (int i = 0; i < page->item_count; i++) {
        if (page->items[i] == item) {
            return i;
        }
    }
    return -1;
}
