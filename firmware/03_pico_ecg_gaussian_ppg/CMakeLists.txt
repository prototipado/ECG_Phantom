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

project(03_pico_ecg_gaussian_ppg C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Pull in the shared GEM/u8g2 static libraries from firmware/common instead of
# vendoring private copies in this firmware folder.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common
                 ${CMAKE_CURRENT_BINARY_DIR}/common_build)

# Add executable. Default name is the project name, version 0.1

add_executable(03_pico_ecg_gaussian_ppg
        pico_ecg.c
        Drivers/src/interface.c
        Drivers/src/signal_gen.c
        Drivers/src/gaussian_ecg_ppg_model.c
        Drivers/src/menu.c
        Drivers/src/terminal_control.c
        Drivers/src/cli.c
)

pico_set_program_name(03_pico_ecg_gaussian_ppg "03_pico_ecg_gaussian_ppg")
pico_set_program_version(03_pico_ecg_gaussian_ppg "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(03_pico_ecg_gaussian_ppg 0)
pico_enable_stdio_usb(03_pico_ecg_gaussian_ppg 1)

# Debug option: run the model validation (Einthoven / PR / RR per rhythm
# preset) once at boot and print the result over USB serial.
option(GAUSSIAN_MODEL_SELF_TEST_RUN_AT_BOOT "Run the Gaussian ECG/PPG model self test at boot" OFF)
if(GAUSSIAN_MODEL_SELF_TEST_RUN_AT_BOOT)
    target_compile_definitions(03_pico_ecg_gaussian_ppg PRIVATE GAUSSIAN_MODEL_SELF_TEST_RUN_AT_BOOT)
endif()

# Add the standard include files to the build
target_include_directories(03_pico_ecg_gaussian_ppg PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/Drivers/inc
)

# Add any user requested libraries. 'gem' pulls in 'u8g2' and its public
# include paths (common/GEM and common/u8g2/csrc) transitively.
target_link_libraries(03_pico_ecg_gaussian_ppg
        gem
        pico_stdlib
        hardware_spi
        hardware_gpio
        hardware_pwm
        hardware_adc
        pico_multicore
        )

pico_add_extra_outputs(03_pico_ecg_gaussian_ppg)

