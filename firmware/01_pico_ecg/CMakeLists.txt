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

# Include shared common libraries
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_CURRENT_BINARY_DIR}/common_build)

# Add executable. Default name is the project name, version 0.1

add_executable(pico_ecg 
        pico_ecg.c
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

# Add the standard include files to the build
target_include_directories(pico_ecg PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/Drivers/inc
)

# Add libraries to the build
target_link_libraries(pico_ecg 
        gem
        pico_stdlib
        hardware_spi
        hardware_gpio
        hardware_pwm
        hardware_adc
        pico_multicore
)

pico_add_extra_outputs(pico_ecg)
