// Session 3: FreeRTOS tracker application for the NUCLEO-L476RG.
//   tasks: motion (LIS2DH12 wake-on-motion) > modem (BG95: LTE-M + GNSS) > app (report policy)
//          > console; the idle task enters Sleep or Stop 2 (power/src/pm.c).
// Wiring: app/include/board.h. Console: ST-LINK virtual COM port, 115200 8N1.
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "app.h"
#include "board.h"
#include "fault.h"
#include "gpio.h"
#include "rcc.h"
#include "stm32l4xx.h"
#include "task.h"

static void report_previous_crash(void)
{
    fault_record_t f;
    if (fault_take_last(&f)) {
        char why[128];
        (void)fault_describe(f.cfsr, f.hfsr, why, sizeof why);
        console_print("\r\n*** previous boot ended in %s: %s\r\n", fault_exception_name(f.exception), why);
        console_print("    pc=0x%08x lr=0x%08x cfsr=0x%08x hfsr=0x%08x\r\n", (unsigned)f.pc, (unsigned)f.lr,
                      (unsigned)f.cfsr, (unsigned)f.hfsr);
        console_print("    resolve: arm-none-eabi-addr2line -e tracker.elf 0x%08x\r\n", (unsigned)f.pc);
    }
    char what[16];
    char where[40];
    int line = 0;
    if (rtos_take_panic(what, sizeof what, where, sizeof where, &line)) {
        console_print("\r\n*** previous boot ended in an RTOS %s: %s:%d\r\n", what, where, line);
    }
}

int main(void)
{
    bool clock_ok = (rcc_clock_init_80mhz() == 0);
    fault_init();
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) {
        // Keep the debug connection alive in Stop 2 (costs current: only with a debugger attached).
        DBGMCU->CR |= DBGMCU_CR_DBG_STOP;
    }

    gpio_clock_enable(LED_PORT);
    gpio_set_mode(LED_PORT, LED_PIN, GPIO_MODE_OUTPUT);

    console_start();
    console_print("\r\nstm32l4-lte-m-tracker S3 (FreeRTOS %s) | SYSCLK %u Hz%s\r\n", tskKERNEL_VERSION_NUMBER,
                  (unsigned)SystemCoreClock, clock_ok ? "" : " (PLL FAILED, running on MSI)");
    report_previous_crash();

    modem_task_start();
    app_task_start();
    motion_start();
    vTaskStartScheduler();  // starts the LPTIM1 tick (vPortSetupTimerInterrupt) and never returns
    for (;;) {
    }
}
