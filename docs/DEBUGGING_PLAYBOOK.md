# STM32 hardware debugging playbook

A field guide for bringing up peripherals on a custom STM32 board, written around the STM32L4 (RM0351) but valid for most STM32 families. It is the method this repository's drivers and tests are built on.

**Rule zero: measure before you change code.** Every step below is *hypothesis → measurement → note*. Read the registers with the debugger before touching the source. Look at the wires with a logic analyser or scope before blaming the driver.

---

## 1. Five-minute triage

| Check | How | What "good" looks like |
|---|---|---|
| Power | Multimeter on VDD/VDDA, current draw of the board | 3.3 V ±5 %, current consistent with the design (a short shows as a hot part or high current) |
| Reset | Scope on NRST | High, no pulses (repeated pulses = brown-out or watchdog) |
| Boot mode | BOOT0 level / option bytes | BOOT0 low → boots from flash |
| Debug link | `pyocd list` / `openocd -f tools/openocd.cfg` | Target detected, IDCODE read |
| Code runs | Break at `main`, or toggle a GPIO first thing in `Reset_Handler` | Reaches `main` |
| Clock | `RCC->CFGR` SWS bits, or route SYSCLK to MCO (PA8) and measure | Expected source and frequency |

If the core does not reach `main`, look at the vector table (first word = initial SP inside RAM, second = `Reset_Handler` with bit 0 set), the linker script memory sizes, and a HardFault before `main` (FPU used before CPACR is set is a classic).

## 2. Dead SPI peripheral: checklist in order

Work through these in order. Every item has broken a real board.

### 2.1 Clocks
- **Peripheral clock enabled?** SPI1 is on APB2 (`RCC->APB2ENR.SPI1EN`), SPI2/SPI3 on APB1 (`RCC->APB1ENR1`). With the clock off, every register reads 0. The driver then times out waiting for TXE: see `test_timeout_when_txe_never_set`.
- **GPIO port clock enabled?** `RCC->AHB2ENR.GPIOxEN`. Without it, MODER/AFR writes are silently lost.
- Read back the enable register once after setting it, before using the peripheral.

### 2.2 Pins
- `MODER` = `10` (alternate function) for SCK, MOSI, MISO (and NSS if hardware-managed).
- **AF number from the datasheet's "Alternate function mapping" table**, not from the reference manual. On the L476, SPI1/SPI2 use AF5 and SPI3 uses AF6. `AFR[0]` (AFRL) is pins 0–7 and `AFR[1]` (AFRH) is pins 8–15. Writing the AF to the wrong half is very common.
- `OSPEEDR` high enough for the SCK frequency, or edges will be rounded at several MHz.
- MISO pull-down or pull-up if slaves tri-state it, otherwise you read noise between transfers.
- Check the pin is not already used by something else on the board: SWD on PA13/PA14, SWO on PB3, LD2 on PA5 on Nucleo-64 boards.

### 2.3 Mode and framing (CR1/CR2)
- **CPOL/CPHA against the slave's datasheet timing diagram**, not its "mode number" (vendors disagree):

| Mode | CPOL | CPHA | SCK idle | Data sampled on |
|---|---|---|---|---|
| 0 | 0 | 0 | low | rising (1st) edge |
| 1 | 0 | 1 | low | falling (2nd) edge |
| 2 | 1 | 0 | high | falling (1st) edge |
| 3 | 1 | 1 | high | rising (2nd) edge |

  Symptom of the wrong mode: data shifted by one bit (values doubled or halved), or correct only some of the time.
- `LSBFIRST` as the slave expects (almost always MSB first).
- **Data size `CR2.DS`** = bits − 1 (4 to 16 bits on the L4 SPI). The reset value is 8-bit.
- **`CR2.FRXTH = 1` for 8-bit frames.** With FRXTH = 0, RXNE waits until 16 bits are in the RX FIFO, so a single byte "never arrives" and the code hangs. This catches everyone porting from F1/F4 (no FIFO) to L4/F7/G4. Covered by `test_8bit_frames_set_ds_and_frxth` and `test_timeout_when_rxne_never_set`.
- **Access width to `DR` matches the frame size.** With 8-bit frames, write and read `DR` as a byte (`*(volatile uint8_t *)&SPI->DR`). A 16-bit access packs two frames into the FIFO: you send a spurious extra byte, and reads consume two frames. Covered by `test_8bit_frames_use_byte_access_to_dr`.
- Change CR1/CR2 only with `SPE = 0`, then set `SPE`.

### 2.4 NSS (chip select)
- Master with software NSS: **`SSM = 1` and `SSI = 1`**. Without SSI, the internal NSS reads low, the SPI raises **MODF** and drops out of master mode (`MSTR` and `SPE` cleared by hardware). Covered by `test_master_software_nss_and_enabled` and `test_mode_fault_reported`.
- Most designs drive the slave's CS from a plain GPIO. Assert it before the first byte. Release it only after `FTLVL = 0` **and** `BSY = 0`. Releasing it when TXE goes high cuts off the last byte.
- Check the slave's CS setup and hold times. Some parts need a few µs after CS falls.

### 2.5 Status flags while it runs
| Flag | Meaning | Typical cause |
|---|---|---|
| `TXE` stuck at 0 | TX FIFO never drains | SPE = 0, peripheral clock off, MODF dropped master mode |
| `RXNE` never 1 | Nothing received | FRXTH wrong for the frame size, not master, no SCK |
| `BSY` stuck at 1 | Transfer never ends | Slave mode waiting for a clock, or wrong end-of-transfer sequence |
| `OVR` | Received frame not read in time | Reading RX too slowly (interrupt latency, debugger stepping) |
| `MODF` | NSS low while master | SSM/SSI not set, or NSS pin wired/pulled low |

Clear sequences: **MODF** = read SR, then write CR1. **OVR** = read DR, then read SR.

### 2.6 Measure the bus
1. Logic analyser on SCK, MOSI, MISO and CS, with the SPI decoder (sigrok/PulseView `spi`) set to the same CPOL/CPHA. If the decoded bytes are what you meant to send, the MCU side works and the problem is the slave or the wiring.
2. Measure the actual SCK frequency. The real clock is `PCLK / 2^(BR+1)`; `spi_br_for()` never exceeds the requested maximum. A slave that works at 1 MHz but fails at 10 MHz points to signal integrity or the slave's limit.
3. Scope (not the logic analyser) for analog problems: ringing on SCK with long wires (add a 22–33 Ω series resistor at the source), 5 V slaves driving 3.3 V pins, slow edges from weak pull-ups.
4. MISO stuck at 0x00 or 0xFF: slave not selected, not powered, held in reset, or MISO not connected. The demo prints a hint when the loopback reads all zeros.

### 2.7 Loopback test (isolate MCU from slave)
Wire MOSI to MISO and run a transfer. This firmware's console command `spi` does that with a jumper from PB15 to PB14. If the loopback works, the clocks, pins, mode and driver are fine and the problem is on the slave side.

## 3. LEDs that don't light (or blink wrong)

1. **Polarity:** is the LED wired from pin to GND (active high) or from VDD to pin (active low, pin sinks current)? Check the schematic.
2. **Pin really is an output:** `MODER = 01`, and nothing else (timer AF, SWD, another driver) owns the pin. On a Nucleo-64, LD2 is on PA5, which is also SPI1_SCK.
3. **Write through BSRR**, not read-modify-write of ODR, if an interrupt also touches the port.
4. **Current:** check the resistor value and the pin current limit (a few mA per pin, total per port limited). For high-current LEDs use a transistor.
5. **PWM dimming via a timer:**
   - PSC/ARR give the frequency and CCRx the duty cycle. Set ARPE/OCxPE and generate an update (`EGR.UG`) so the values load.
   - Enable the channel output in `CCER.CCxE`.
   - **On advanced timers (TIM1/TIM8, and TIM15/16/17) also set `BDTR.MOE`**, or nothing comes out of the pin.
6. **WS2812 / addressable LEDs:**
   - 800 kHz, T0H ≈ 0.4 µs and T1H ≈ 0.8 µs (±150 ns). Measure them with the logic analyser.
   - Their logic-high threshold is 0.7·VDD (3.5 V at 5 V), so a 3.3 V MCU is out of spec. Use a level shifter.
   - A dead first LED stops the whole chain.

## 4. Crashes and resets

This firmware captures HardFault, MemManage, BusFault and UsageFault (`drivers/src/fault.c`):
- The handler stores the stacked registers (r0–r3, r12, lr, **pc**, xpsr) and CFSR/HFSR/MMFAR/BFAR in SRAM2. SRAM2 survives the reset.
- With a debugger attached it stops on a breakpoint. Without one, it resets.
- On the next boot it prints something like this (example values):

```
*** previous boot ended in UsageFault: DIVBYZERO
    pc=0x080004f2 lr=0x08000611 xpsr=0x61000000
    ...
    resolve: arm-none-eabi-addr2line -e tracker.elf 0x080004f2
```

How to read it:
- `addr2line -e tracker.elf <pc>` gives the file and line of the faulting instruction.
- **CFSR** says why. `PRECISERR` + `BFARVALID`: BFAR holds the bad address. `IMPRECISERR`: the PC is *after* the faulting write (buffered write), so look a few instructions back. `UNDEFINSTR` / `INVSTATE`: a jump to a non-Thumb address or corrupted function pointer. `DIVBYZERO`: needs `CCR.DIV_0_TRP`, otherwise the division silently returns 0. `FORCED` in HFSR: a configurable fault was escalated to HardFault (its handler is disabled or the fault happened inside it).
- **Stack overflow:** a fault inside a deep call chain, with an SP near the end of RAM. Fill the stack with a pattern at boot and check the high-water mark, or place the stack so it overflows into an unmapped region.
- The console command `fault` triggers a deliberate divide-by-zero so the whole chain can be checked. `tools/gdbinit` defines `fault` for the same decoding from GDB.

## 5. Tools

| Tool | Use |
|---|---|
| OpenOCD + `tools/openocd.cfg`, or pyOCD | Flash, GDB server, read registers |
| `arm-none-eabi-gdb -x tools/gdbinit` | `fault` and `spi2` helper commands, watchpoints on registers |
| SVD file (STM32L4x6.svd) in your IDE/GDB plugin | Named register fields instead of hex |
| Logic analyser + sigrok/PulseView | SPI/I2C/UART decoders, timing measurements |
| Oscilloscope | Signal integrity, power rails, reset |
| Console (`regs`, `stats`) | Live register dump and UART RX counters without a debugger |

## 6. Report template (what the client receives)

```
Symptom:      <what was observed, under which conditions>
Evidence:     <capture/screenshot/register dump, with values>
Root cause:   <one sentence, pointing to the schematic net, register field or line of code>
Fix:          <change made, commit/patch reference>
Verification: <how it was proven fixed: same capture after the fix, test added>
Prevention:   <test, check or design rule so it does not come back>
```
