# Third-party code

| Component | Version | License | Location | Use |
|---|---|---|---|---|
| CMSIS-Core (Arm) | 5.9.0 | Apache-2.0 | `third_party/cmsis/core` | Cortex-M4 core register definitions and intrinsics |
| CMSIS device headers for STM32L4 (STMicroelectronics) | v1.7.5 | Apache-2.0 | `third_party/cmsis/device` | STM32L476 peripheral register definitions |
| Unity (ThrowTheSwitch) | v2.7.0 | MIT | `third_party/unity` | Host unit-test framework |

The files are unmodified copies. The license text for each component sits next to it. No HAL or LL driver code is used: every driver in `drivers/` is written at register level for this project.
