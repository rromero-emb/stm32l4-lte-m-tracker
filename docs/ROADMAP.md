# Roadmap

Each session is a self-contained step that ends with CI green.

| Session | Scope | Status |
|---|---|---|
| **S1** | Bare-metal base: CMake + toolchain file, C startup and linker script, clocks (80 MHz PLL), GPIO, SysTick, USART2 with RX over circular DMA + IDLE line, SPI master (FIFO-aware), fault capture to SRAM2, host unit tests, CI, debugging playbook | ✅ host tests + cross-build in CI; **not yet run on hardware** |
| **S2** | Modem layer for the Quectel BG95: byte-level AT parser, command engine with timeouts and URC dispatch, BG95 state machine (power-on, registration, PDP context, UDP send, recovery), tested against a scripted fake modem. 20-byte payload codec + Python reference decoder with shared vectors | ✅ host tests + cross-build. **Pending:** Renode platform for the STM32L476 and Robot Framework tests (moved to S4 with the simulator); PSM configuration done in S3 |
| **S3** | FreeRTOS V11 (static allocation): motion / modem / app / console tasks. RTOS tick on LPTIM1 + LSE, tickless idle into Stop 2 or Sleep with peripheral locks. LIS2DH12 wake-on-motion driver. BG95: GNSS fix (`AT+QGPSLOC`), PSM request (`AT+CPSMS`, 24.008 timer encoding), clean power-off. Report policy (heartbeat / motion start, periodic, stop). Power model with sourced parameters: [POWER.md](POWER.md) | ✅ host tests + cross-build. **Not yet run on hardware.** GNSS runs as a request served by the modem task (one AT channel), not a separate task. Battery voltage, temperature and RSRP are not measured yet |
| S4 | ADC (battery, temperature) and RSRP (`AT+QCSQ`) in the report. Python BG95 simulator over a TCP socket connected to Renode's UART: coverage loss, PSM, error injection. Satellite fallback interface (NTN on BG95-S5 or Iridium SBD) with a simulated backend. Watchdog | planned |
| S5 | On hardware: NUCLEO-L476RG + BG95 module + LIS2DH12. Logic-analyser captures of the SPI/UART exercises, measured Stop 2 and PSM currents and report-cycle charge replacing the assumptions in `tools/power_params.json`. Checks that only hardware can answer: EXTI on the console RX pin in AF mode, LSE start-up time, Stop 2 wake-up latency | planned |

Design notes collected so far:
- The Swarm satellite network was shut down in 2025. The BG95-S5 adds 3GPP NTN (satellite NB-IoT) to the same module, so satellite fallback does not need a second modem.
- On the BG95, GNSS and LTE share the RF path and are not used at the same time: the application gets a fix first, then attaches and sends.
- For payloads under 30 bytes, the modem's own socket/MQTT AT commands are cheaper in RAM and energy than PPP + an IP stack on the MCU.
