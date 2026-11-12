# STM32L4 LTE-M tracker firmware

[![ci](https://github.com/rromero-emb/stm32l4-lte-m-tracker/actions/workflows/ci.yml/badge.svg)](https://github.com/rromero-emb/stm32l4-lte-m-tracker/actions/workflows/ci.yml)

Firmware for a low-power asset tracker on the **STM32L476** with a **Quectel BG95** (LTE-M / NB-IoT + GNSS), built up session by session from bare-metal drivers. No HAL, no CubeMX. Every driver is written at register level against the reference manual (RM0351), and the hardware-independent parts are unit-tested on the host in CI.

> **Status: session 2 of 5 (in progress).** The bare-metal base (S1) and the modem layer and payload codec (S2) are done and verified by host tests and a clean cross-build in CI. The Renode emulation part of S2 is still pending. Nothing has **been run on a board yet**: on-hardware validation (logic-analyser captures, current measurements) is session 5. See [docs/ROADMAP.md](docs/ROADMAP.md).

## Session 2: modem layer and payload

| Area | Implementation |
|---|---|
| AT parser | Byte-level line framing that copes with lines split across reads, the `>` data prompt (no line end), and over-long lines dropped whole |
| AT engine | One command in flight, timeout safe across the 32-bit millisecond wrap; OK / ERROR / `+CME ERROR: <n>` / `SEND OK`. A line that matches the expected prefix of the command in flight is its response; otherwise it is dispatched as a URC, so prefixes that are both (`+CEREG:`) are handled |
| BG95 state machine | PWRKEY pulse → `APP RDY` → init script (LTE-M only, numeric errors, registration URCs) → `+CEREG` polling → PDP context → UDP send (`QIOPEN` / `QISEND` + `>` / `QICLOSE`). Non-blocking, no RTOS needed |
| Recovery | Escalation: radio off/on (`CFUN`), then a full power cycle (`QPOWD` + PWRKEY), then `FAILED` after 3 attempts. An unexpected `APP RDY` while up (module rebooted on its own) re-runs the init |
| Payload | 20-byte bit-packed report: position at about 1–2 m resolution, time, fix quality, speed/heading, battery, temperature, LTE RSRP. See [docs/PAYLOAD.md](docs/PAYLOAD.md) |
| Tests | AT layer: 13 tests. BG95: 7 scenarios against a **scripted fake modem** with simulated time (bring-up, PWRKEY pulse width, UDP send, socket error, CME error during init, dead module, unexpected restart, registration timeout). Payload: 6 tests plus **shared vectors checked byte for byte between the C codec and the Python reference decoder** (`tools/payload.py`) |

The fake-modem tests caught a real race. When the `OK` of `AT+QIOPEN` and its `+QIOPEN: 0,0` URC arrived in the same UART read, which happens on hardware, the state machine cleared the URC flag on its next poll and waited forever. The flag is now cleared only when the command is sent.

## Session 1: bare-metal base

| Area | Implementation |
|---|---|
| Build | CMake presets + Arm GNU toolchain file, Ninja, `-Wall -Wextra -Wconversion -Werror`, map file and size report |
| Startup | Vector table and `Reset_Handler` in C, linker script with SRAM1/SRAM2 and a `.noinit` section, stack-size check at link time |
| Clocks | MSI 4 MHz → PLL → 80 MHz, voltage range 1, 4 flash wait states, every ready flag with a timeout |
| GPIO | Mode, pull, speed and alternate function; atomic writes through BSRR |
| UART | USART2 (ST-LINK virtual COM port). **RX via DMA in circular mode** with IDLE-line, half- and full-transfer events into a lock-free ring buffer. Overrun and dropped bytes counted |
| SPI | Master driver aware of the L4 FIFO: byte-wide DR access, `FRXTH`, software NSS, correct end-of-transfer (`FTLVL`, `BSY`), MODF/OVR detection and clearing, timeouts on every wait |
| Faults | HardFault/MemManage/BusFault/UsageFault save the stacked registers and CFSR/HFSR/MMFAR/BFAR to SRAM2, reset, and report on the next boot with the `addr2line` command to run |
| Console | `help`, `spi` (loopback test), `mode <0-3>`, `regs` (live register dump), `fault` (deliberate crash), `stats`, `payload` (encode a sample report) |
| Tests | 5 host suites (Unity) with AddressSanitizer + UBSan: SPI against a fake register block, ring buffer and DMA wrap-around, baud-rate error, formatter, fault decoding |
| Quality | clang-format and cppcheck in CI (every suppression is inline, with its reason), coverage summary, Dependabot |

Firmware size (Release, `-Os`, sessions 1–2): about 8.6 KB of flash and 0.4 KB of RAM.

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
app/            demo application, console, payload codec
drivers/        register-level drivers (include/, src/)
modem/          AT parser/engine and BG95 state machine
startup/        vector table and reset handler
linker/         linker script
cmake/          toolchain file
tests/host/     unit tests, fake BG95, stub core header for host builds
tests/vectors/  payload test vectors shared by C and Python
tools/          OpenOCD config, GDB helpers, payload.py reference decoder
docs/           debugging playbook, payload format, roadmap
third_party/    CMSIS headers, Unity (see THIRD_PARTY.md)
```

## License

MIT for the code in this repository. Third-party components keep their own licenses: see [THIRD_PARTY.md](THIRD_PARTY.md).
