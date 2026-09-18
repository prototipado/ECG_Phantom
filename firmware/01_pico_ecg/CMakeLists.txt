# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_ecg C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(pico_ecg 
        pico_ecg.c
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
        GEM/gem_core.c
        GEM/gem_item.c
        GEM/gem_page.c
        GEM/gem_u8g2.c
        Drivers/src/interface.c
        Drivers/src/signal_gen.c
        Drivers/src/signal_registry.c
        Drivers/src/menu.c
        Drivers/src/terminal_control.c
        Drivers/src/cli.c
)

pico_set_program_name(pico_ecg "pico_ecg")
pico_set_program_version(pico_ecg "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(pico_ecg 0)
pico_enable_stdio_usb(pico_ecg 1)

# Add the standard library to the build
target_link_libraries(pico_ecg
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(pico_ecg PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/GEM
        ${CMAKE_CURRENT_LIST_DIR}/u8g2/csrc
        ${CMAKE_CURRENT_LIST_DIR}/Drivers/inc
)

# Add any user requested libraries
target_link_libraries(pico_ecg 
        hardware_spi
        hardware_gpio
        hardware_pwm
        hardware_adc
        pico_multicore
        )

pico_add_extra_outputs(pico_ecg)

