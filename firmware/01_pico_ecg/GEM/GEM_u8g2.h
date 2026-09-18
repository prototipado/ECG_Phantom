#ifndef GEM_U8G2_H
#define GEM_U8G2_H

#include "gem_hal.h"
#include "u8g2.h" // User must ensure u8g2 is in include path

// Initialize HAL with a u8g2 instance
// @param u8g2 Pointer to initialized u8g2_t struct
gem_hal_t gem_hal_u8g2_init(u8g2_t* u8g2);

#endif // GEM_U8G2_H
