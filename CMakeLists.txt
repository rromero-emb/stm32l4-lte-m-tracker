cmake_minimum_required(VERSION 3.22)

# One project, two builds:
#  - cross build (toolchain file cmake/arm-none-eabi.cmake): firmware for the STM32L476RG
#  - native build: host unit tests of the hardware-independent code and the drivers (fake registers)
project(stm32l4-lte-m-tracker C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # GNU C: range designators in the vector table, inline asm
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(WARNINGS -Wall -Wextra -Wshadow -Wdouble-promotion -Wformat=2 -Wundef -Wconversion -Wno-sign-conversion)

if(CMAKE_CROSSCOMPILING)
    set(CMAKE_C_FLAGS_DEBUG "-Og -g3")
    set(CMAKE_C_FLAGS_RELEASE "-Os -g")  # keep -g: the .elf is used with addr2line/GDB, the .bin has no debug info
    add_executable(tracker
        startup/startup_stm32l476.c
        drivers/src/rcc.c
        drivers/src/gpio.c
        drivers/src/systick.c
        drivers/src/uart.c
        drivers/src/spi.c
        drivers/src/ringbuf.c
        drivers/src/fmt.c
        drivers/src/fault.c
        drivers/src/fault_decode.c
        app/main.c
    )
    set_target_properties(tracker PROPERTIES SUFFIX ".elf")
    target_compile_definitions(tracker PRIVATE STM32L476xx)
    target_include_directories(tracker PRIVATE
        drivers/include
        third_party/cmsis/core
        third_party/cmsis/device
    )
    target_compile_options(tracker PRIVATE ${WARNINGS} -Werror)
    target_link_options(tracker PRIVATE
        -T${CMAKE_SOURCE_DIR}/linker/stm32l476rg.ld
        -Wl,-Map=${CMAKE_BINARY_DIR}/tracker.map,--cref
        -Wl,--print-memory-usage
    )
    add_custom_command(TARGET tracker POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:tracker> ${CMAKE_BINARY_DIR}/tracker.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:tracker> ${CMAKE_BINARY_DIR}/tracker.hex
        COMMAND ${CMAKE_SIZE} -A $<TARGET_FILE:tracker>
        COMMENT "tracker.bin / tracker.hex"
    )
else()
    enable_testing()
    add_subdirectory(tests/host)
endif()
