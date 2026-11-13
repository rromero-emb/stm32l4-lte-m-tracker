# Third-party code

| Component | Version | License | Location | Use |
|---|---|---|---|---|
| CMSIS-Core (Arm) | 5.9.0 | Apache-2.0 | `third_party/cmsis/core` | Cortex-M4 core register definitions and intrinsics |
| CMSIS device headers for STM32L4 (STMicroelectronics) | v1.7.5 | Apache-2.0 | `third_party/cmsis/device` | STM32L476 peripheral register definitions |
| FreeRTOS-Kernel | V11.1.0 (`dbf7055`) | MIT | `third_party/freertos` | RTOS: `tasks.c`, `queue.c`, `list.c`, `include/`, port `GCC/ARM_CM4F` |
| Unity (ThrowTheSwitch) | v2.7.0 | MIT | `third_party/unity` | Host unit-test framework |

The files are unmodified copies. The license text for each component sits next to it. FreeRTOS is configured in `app/include/FreeRTOSConfig.h`. Its SysTick tick and tickless code are not used: `power/src/pm.c` provides `vPortSetupTimerInterrupt()` and `vPortSuppressTicksAndSleep()` on LPTIM1. No HAL or LL driver code is used: every driver in `drivers/` is written at register level for this project.
