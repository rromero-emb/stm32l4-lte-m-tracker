# STM32L4 LTE-M tracker firmware

[![ci](https://github.com/rromero-emb/stm32l4-lte-m-tracker/actions/workflows/ci.yml/badge.svg)](https://github.com/rromero-emb/stm32l4-lte-m-tracker/actions/workflows/ci.yml)

Firmware for a low-power asset tracker on the **STM32L476** with a **Quectel BG95** (LTE-M / NB-IoT + GNSS), built up session by session from bare-metal drivers. No HAL, no CubeMX. Every driver is written at register level against the reference manual (RM0351), and the hardware-independent parts are unit-tested on the host in CI.

> **Status: session 1 of 5.** The bare-metal base is done and verified by host unit tests and a clean cross-build in CI. It has **not been run on a board yet**; on-hardware validation (logic-analyser captures, current measurements) is session 5. See [docs/ROADMAP.md](docs/ROADMAP.md).

## What is in session 1

| Area | Implementation |
|---|---|
| Build | CMake presets + Arm GNU toolchain file, Ninja, `-Wall -Wextra -Wconversion -Werror`, map file and size report |
| Startup | Vector table and `Reset_Handler` in C, linker script with SRAM1/SRAM2 and a `.noinit` section, stack-size check at link time |
| Clocks | MSI 4 MHz → PLL → 80 MHz, voltage range 1, 4 flash wait states, every ready flag with a timeout |
| GPIO | Mode, pull, speed and alternate function; atomic writes through BSRR |
| UART | USART2 (ST-LINK virtual COM port). **RX via DMA in circular mode** with IDLE-line, half- and full-transfer events into a lock-free ring buffer. Overrun and dropped bytes counted |
| SPI | Master driver aware of the L4 FIFO: byte-wide DR access, `FRXTH`, software NSS, correct end-of-transfer (`FTLVL`, `BSY`), MODF/OVR detection and clearing, timeouts on every wait |
| Faults | HardFault/MemManage/BusFault/UsageFault save the stacked registers and CFSR/HFSR/MMFAR/BFAR to SRAM2, reset, and report on the next boot with the `addr2line` command to run |
| Console | `help`, `spi` (loopback test), `mode <0-3>`, `regs` (live register dump), `fault` (deliberate crash), `stats` |
| Tests | 5 host suites (Unity) with AddressSanitizer + UBSan: SPI against a fake register block, ring buffer and DMA wrap-around, baud-rate error, formatter, fault decoding |
| Quality | clang-format and cppcheck in CI (every suppression is inline, with its reason), coverage summary, Dependabot |

Firmware size (Release, `-Os`): about 6.9 KB of flash and 0.4 KB of RAM.

### STM32L4 pitfalls pinned down by tests

The SPI tests encode mistakes that typically cost a day on a custom board:
- **`FRXTH = 0` with 8-bit frames:** RXNE waits for 16 bits and a single byte never arrives (`test_timeout_when_rxne_never_set`).
- **16-bit access to DR with 8-bit frames:** packs two frames into the FIFO (`test_8bit_frames_use_byte_access_to_dr`).
- **Software NSS without `SSI`:** mode fault, and the peripheral silently leaves master mode (`test_master_software_nss_and_enabled`, `test_mode_fault_reported`).
- **Peripheral clock off:** every flag reads 0. The driver times out instead of hanging (`test_timeout_when_txe_never_set`).

The UART tests also caught a real limit: at 16 MHz, 921600 baud is 2.1 % off with oversampling by 16, too far for a receiver (`test_16mhz_cannot_do_921600_with_over16`).

The method behind all of this is written up in **[docs/DEBUGGING_PLAYBOOK.md](docs/DEBUGGING_PLAYBOOK.md)**: triage, a dead-SPI checklist, LEDs/PWM, crash analysis and a report template.

## Build and test

Requirements (Debian/Ubuntu): `gcc-arm-none-eabi libnewlib-arm-none-eabi cmake ninja-build`. For checks also `clang-format cppcheck`.

```sh
make test          # host unit tests (Unity, ASan + UBSan)
make fw            # firmware: build/fw-release/tracker.{elf,bin,hex,map}
make fw-debug      # -Og -g3 build for the debugger
make lint          # cppcheck
make format-check  # clang-format
make flash         # OpenOCD + ST-LINK (NUCLEO-L476RG)
```

## Hardware (NUCLEO-L476RG)

| Function | Pin | Notes |
|---|---|---|
| Console | PA2 / PA3 (USART2, AF7) | ST-LINK virtual COM port, 115200 8N1 |
| LED | PA5 (LD2) | blinks at 1 Hz while the main loop runs |
| SPI2 SCK / MISO / MOSI | PB13 / PB14 / PB15 (AF5) | loopback test: jumper PB15 → PB14 (CN10 pins 26 and 28) |

Debugging: `openocd -f tools/openocd.cfg`, then `arm-none-eabi-gdb -x tools/gdbinit build/fw-debug/tracker.elf`. `tools/gdbinit` adds `fault` (decode the fault registers) and `spi2` (dump SPI2).

## Layout

```
app/            demo application and console
drivers/        register-level drivers (include/, src/)
startup/        vector table and reset handler
linker/         linker script
cmake/          toolchain file
tests/host/     unit tests + stub core header for host builds
tools/          OpenOCD config, GDB helpers
docs/           debugging playbook, roadmap
third_party/    CMSIS headers, Unity (see THIRD_PARTY.md)
```

## License

MIT for the code in this repository. Third-party components keep their own licenses: see [THIRD_PARTY.md](THIRD_PARTY.md).
