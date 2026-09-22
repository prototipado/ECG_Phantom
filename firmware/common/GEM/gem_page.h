#ifndef GEM_PAGE_H
#define GEM_PAGE_H

#include "gem_common.h"
#include "gem_item.h"

#define GEM_MAX_ITEMS_PER_PAGE 16 // Simplify dynamic allocation for now

typedef struct gem_page {
    const char* title;
    gem_item_t* items[GEM_MAX_ITEMS_PER_PAGE];
    uint8_t item_count;
    
    // Navigation state
    struct gem_page* parent_page;
    uint8_t current_item_index;  // Item actualmente seleccionado
    

} gem_page_t;

void gem_page_init(gem_page_t* page, const char* title, struct gem_page* parent);
void gem_page_add_item(gem_page_t* page, gem_item_t* item);

// ===== SETTERS / GETTERS PARA NAVEGACIÓN MULTINIVEL =====

void gem_page_set_parent(gem_page_t* page, struct gem_page* parent);
struct gem_page* gem_page_get_parent(gem_page_t* page);

// ===== GETTERS GENERALES =====

const char* gem_page_get_title(gem_page_t* page);
void gem_page_set_title(gem_page_t* page, const char* title);

uint8_t gem_page_get_item_count(gem_page_t* page);
gem_item_t* gem_page_get_item(gem_page_t* page, uint8_t index);

// ===== NAVEGACIÓN DE ITEMS EN LA PÁGINA =====

void gem_page_set_current_item_index(gem_page_t* page, uint8_t index);
uint8_t gem_page_get_current_item_index(gem_page_t* page);
gem_item_t* gem_page_get_current_item(gem_page_t* page);

// ===== UTILIDADES =====

void gem_page_remove_item(gem_page_t* page, gem_item_t* item);
gem_item_t* gem_page_find_item_by_title(gem_page_t* page, const char* title);
int gem_page_get_item_index(gem_page_t* page, gem_item_t* item);

#endif // GEM_PAGE_H
