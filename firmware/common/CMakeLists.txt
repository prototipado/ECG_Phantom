cmake_minimum_required(VERSION 3.13)

# ==============================================================================
# Librería común: u8g2
# ==============================================================================
add_library(u8g2 STATIC
    u8g2/csrc/u8g2_d_memory.c
    u8g2/csrc/u8g2_font.c
    u8g2/csrc/u8g2_fonts.c
    u8g2/csrc/u8g2_hvline.c
    u8g2/csrc/u8g2_ll_hvline.c
    u8g2/csrc/u8g2_circle.c
    u8g2/csrc/u8g2_line.c
    u8g2/csrc/u8g2_polygon.c
    u8g2/csrc/u8g2_intersection.c
    u8g2/csrc/u8g2_bitmap.c
    u8g2/csrc/u8g2_setup.c
    u8g2/csrc/u8g2_d_setup.c
    u8g2/csrc/u8g2_cleardisplay.c
    u8g2/csrc/u8g2_box.c
    u8g2/csrc/u8g2_message.c
    u8g2/csrc/u8g2_buffer.c
    u8g2/csrc/u8x8_setup.c
    u8g2/csrc/u8x8_display.c
    u8g2/csrc/u8x8_byte.c
    u8g2/csrc/u8x8_gpio.c
    u8g2/csrc/u8x8_d_ssd1306_128x64_noname.c
    u8g2/csrc/u8x8_cad.c
    u8g2/csrc/u8x8_8x8.c
    u8g2/csrc/u8x8_fonts.c
    u8g2/csrc/u8x8_string.c
    u8g2/csrc/u8x8_u8toa.c
    u8g2/csrc/u8x8_u16toa.c
)

target_include_directories(u8g2 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/u8g2/csrc
)

target_link_libraries(u8g2 PUBLIC
    pico_stdlib
)

# ==============================================================================
# Librería común: GEM (Graphic Embedded Menu)
# ==============================================================================
add_library(gem STATIC
    GEM/gem_core.c
    GEM/gem_item.c
    GEM/gem_page.c
    GEM/gem_u8g2.c
)

target_include_directories(gem PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/GEM
)

target_link_libraries(gem PUBLIC
    u8g2
    pico_stdlib
)
