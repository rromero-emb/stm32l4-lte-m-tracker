// Console task on USART2 (ST-LINK virtual COM port, 115200 8N1).
// USART2 stops in Stop 2, so the RX pin is also EXTI3: the first key press (its start bit)
// wakes the MCU, is lost, and the console then holds PM_LOCK_CONSOLE (Sleep only) until
// CONSOLE_AWAKE_MS after the last key.
#include <stdarg.h>
#include <string.h>

#include "app.h"
#include "board.h"
#include "fault.h"
#include "fmt.h"
#include "gpio.h"
#include "lis2dh12.h"
#include "payload.h"
#include "pm.h"
#include "rcc.h"
#include "semphr.h"
#include "uart.h"

#define CONSOLE_AWAKE_MS 30000U

static StaticTask_t tcb;
static StackType_t stack[384];
static TaskHandle_t self;
static StaticSemaphore_t mutex_buf;
static SemaphoreHandle_t mutex;
static volatile bool woken_by_rx_edge;

void console_print(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    size_t n = fmt_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    bool rtos = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (rtos) {
        (void)xSemaphoreTake(mutex, portMAX_DELAY);
    }
    uart_write(&uart_console, line, n);
    if (rtos) {
        (void)xSemaphoreGive(mutex);
    }
}

static void on_rx(void *ctx)
{
    (void)ctx;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(self, &woken);
    portYIELD_FROM_ISR(woken);
}

void EXTI3_IRQHandler(void)
{
    EXTI->PR1 = EXTI_PR1_PIF3;
    EXTI->IMR1 &= ~EXTI_IMR1_IM3;  // one wake-up is enough: the console task re-arms it
    pm_lock_from_isr(PM_LOCK_CONSOLE);
    woken_by_rx_edge = true;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(self, &woken);
    portYIELD_FROM_ISR(woken);
}

static void rx_wake_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~SYSCFG_EXTICR1_EXTI3) | SYSCFG_EXTICR1_EXTI3_PA;
    EXTI->FTSR1 |= EXTI_FTSR1_FT3;
    NVIC_SetPriority(EXTI3_IRQn, 7U);
    NVIC_EnableIRQ(EXTI3_IRQn);
}

static void cmd_status(void)
{
    pm_stats_t s;
    pm_get_stats(&s);
    uint32_t up = pm_uptime_s();
    console_print(
        "uptime %u s, clock %u Hz, low-speed clock %s, locks 0x%x\r\n", (unsigned)up, (unsigned)SystemCoreClock,
        (pm_lsclk() == RCC_LSCLK_LSE) ? "LSE" : ((pm_lsclk() == RCC_LSCLK_LSI) ? "LSI (no crystal?)" : "NONE"),
        (unsigned)pm_locks());
    console_print("stop2: %u entries, %u s | sleep: %u entries, %u s | clock restore failures %u\r\n",
                  (unsigned)s.stop2_entries, (unsigned)(s.stop2_ticks >> 10), (unsigned)s.sleep_entries,
                  (unsigned)(s.sleep_ticks >> 10), (unsigned)s.clock_restore_failures);
    console_print("modem: %s | motion sensor: %s\r\n", bg95_state_name(modem_state()), motion_present() ? "yes" : "no");
    app_status();
    console_print("console rx: dropped %u, overruns %u\r\n", (unsigned)uart_rx_dropped(&uart_console),
                  (unsigned)uart_rx_overruns(&uart_console));
}

static void cmd_stacks(void)
{
    static const char *const names[] = {"console", "modem", "app", "motion"};
    for (unsigned i = 0; i < (sizeof names / sizeof names[0]); i++) {
        TaskHandle_t t = xTaskGetHandle(names[i]);
        if (t != NULL) {
            console_print("%s: %u words free\r\n", names[i], (unsigned)uxTaskGetStackHighWaterMark(t));
        }
    }
}

static void cmd_payload(void)
{
    // Sample report (Madrid); decode the hex with: python3 tools/payload.py decode <hex>
    tracker_report_t r = {MSG_PERIODIC, FIX_3D, true, 42U, 1762946400U, 404167754, -37037902, 657,
                          48U,          271U,   6U,   9U,  14U,         3871U,     17,        -98};
    uint8_t buf[PAYLOAD_LEN];
    if (!payload_encode(&r, buf)) {
        console_print("payload: encode failed\r\n");
        return;
    }
    char hex[2U * PAYLOAD_LEN + 1U];
    for (unsigned i = 0; i < PAYLOAD_LEN; i++) {
        (void)fmt_snprintf(&hex[2U * i], 3U, "%02x", buf[i]);
    }
    console_print("payload (20 B): %s\r\n", hex);
}

static void cmd_fault(void)
{
    console_print("dividing by zero (DIV_0_TRP is on)...\r\n");
    volatile uint32_t zero = 0U;
    // cppcheck-suppress zerodiv ; deliberate: this command exists to demonstrate fault capture
    volatile uint32_t r = 100U / zero;
    (void)r;
}

static void cmd_help(void)
{
    console_print("commands: help | status | report | stacks | payload | fault\r\n");
}

static void handle_line(const char *line)
{
    if (strcmp(line, "help") == 0) {
        cmd_help();
    } else if (strcmp(line, "status") == 0) {
        cmd_status();
    } else if (strcmp(line, "report") == 0) {
        (void)xTaskNotify(app_task_handle(), EV_REPORT_NOW, eSetBits);
    } else if (strcmp(line, "stacks") == 0) {
        cmd_stacks();
    } else if (strcmp(line, "payload") == 0) {
        cmd_payload();
    } else if (strcmp(line, "fault") == 0) {
        cmd_fault();
    } else if (line[0] != '\0') {
        console_print("unknown command '%s'\r\n", line);
        cmd_help();
    } else {
        // empty line
    }
}

static void console_task(void *arg)
{
    (void)arg;
    char line[64];
    size_t len = 0U;
    uint32_t last_activity = pm_uptime_ms();
    pm_lock(PM_LOCK_CONSOLE);  // awake for a while after boot
    cmd_help();
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000U));
        if (woken_by_rx_edge) {
            woken_by_rx_edge = false;
            last_activity = pm_uptime_ms();  // restart the awake window from the wake-up edge
            console_print("console: awake\r\n");
        }
        uint8_t c;
        while (uart_read(&uart_console, &c, 1U) == 1U) {
            last_activity = pm_uptime_ms();
            if ((c == '\r') || (c == '\n')) {
                console_print("\r\n");
                line[len] = '\0';
                handle_line(line);
                len = 0U;
            } else if (len < (sizeof line - 1U)) {
                line[len++] = (char)c;
                uart_write(&uart_console, (const char *)&c, 1U);  // echo
            } else {
                // line full: ignore until Enter
            }
        }
        if ((pm_locks() & PM_LOCK_CONSOLE) == 0U) {
            continue;
        }
        if ((pm_uptime_ms() - last_activity) >= CONSOLE_AWAKE_MS) {
            console_print("console: idle, Stop 2 allowed (press a key to wake; the first key is lost)\r\n");
            pm_unlock(PM_LOCK_CONSOLE);
            EXTI->PR1 = EXTI_PR1_PIF3;
            EXTI->IMR1 |= EXTI_IMR1_IM3;
        }
    }
}

void console_start(void)
{
    mutex = xSemaphoreCreateMutexStatic(&mutex_buf);
    uart_init(&uart_console, RCC_PCLK1_HZ, 115200U, on_rx, NULL);
    rx_wake_init();
    self = xTaskCreateStatic(console_task, "console", sizeof stack / sizeof stack[0], NULL, PRIO_CONSOLE, stack, &tcb);
}
