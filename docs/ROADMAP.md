# Roadmap

Each session is a self-contained step that ends with CI green.

| Session | Scope | Status |
|---|---|---|
| **S1** | Bare-metal base: CMake + toolchain file, C startup and linker script, clocks (80 MHz PLL), GPIO, SysTick, USART2 with RX over circular DMA + IDLE line, SPI master (FIFO-aware), fault capture to SRAM2, host unit tests, CI, debugging playbook | ✅ host tests + cross-build in CI; **not yet run on hardware** |
| S2 | Modem layer for the Quectel BG95: byte-level AT parser, command engine with timeouts and URC dispatch, BG95 state machine (power-on, registration, PSM, recovery), tested on the host against scripted modem transcripts. Renode platform for the STM32L476 and first Robot Framework tests. Compact binary payload codec (≈20 bytes) + Python decoder | planned |
| S3 | FreeRTOS: modem / GNSS / motion / app tasks, tickless idle with Stop 2 + LPTIM, accelerometer wake-on-motion (LIS2DH12), power budget documented | planned |
| S4 | Python BG95 simulator over a TCP socket connected to Renode's UART: coverage loss, PSM, error injection. Satellite fallback interface (NTN on BG95-S5 or Iridium SBD) with a simulated backend. Watchdog | planned |
| S5 | On hardware: NUCLEO-L476RG + BG95 module. Logic-analyser captures of the SPI/UART exercises, measured Stop 2 and PSM currents, battery-life estimate from measurements | planned |

Design notes collected so far:
- The Swarm satellite network was shut down in 2025. The BG95-S5 adds 3GPP NTN (satellite NB-IoT) to the same module, so satellite fallback does not need a second modem.
- On the BG95, GNSS and LTE share the RF path and are not used at the same time: the application gets a fix first, then attaches and sends.
- For payloads under 30 bytes, the modem's own socket/MQTT AT commands are cheaper in RAM and energy than PPP + an IP stack on the MCU.
