// Modem task: sole owner of the BG95 and its UART. The BG95 also hosts the GNSS receiver, and
// both share one AT channel, so GNSS is a request served here, not a separate task.
//
// While the modem is powered the task polls the state machine every MODEM_POLL_MS and holds
// PM_LOCK_MODEM (USART1 must keep receiving: Sleep, not Stop 2). Once the module is off the
// task blocks without timeout and the MCU can stay in Stop 2 until the next request.
#include <string.h>

#include "app.h"
#include "board.h"
#include "gpio.h"
#include "pm.h"
#include "rcc.h"
#include "uart.h"

#define MODEM_POLL_MS 20U
#define MODEM_UP_TIMEOUT_MS 300000U  // power-on to READY, including registration and recovery
#define EV_RX 0x1U
#define EV_REQ 0x2U
#define REPLY_INDEX 1U

#define MODEM_APN "iot.example"    // replace with the SIM's APN
#define MODEM_HOST "203.0.113.10"  // documentation address (RFC 5737): replace with the collector
#define MODEM_PORT 5683U

typedef enum { OP_NONE = 0, OP_FIX, OP_SEND, OP_OFF } op_t;

static StaticTask_t tcb;
static StackType_t stack[512];
static TaskHandle_t self;

static bg95_t modem;
static const bg95_config_t cfg = {
    .apn = MODEM_APN, .host = MODEM_HOST, .port = MODEM_PORT, .psm = false, .psm_tau_s = 0U, .psm_active_s = 0U};

// Request mailbox: written by the requester before EV_REQ, read by the modem task after it.
static volatile op_t req_op;
static uint32_t req_timeout_ms;
static uint8_t req_data[BG95_PAYLOAD_MAX];
static size_t req_len;
static TaskHandle_t requester;
static bool reply_ok;
static gnss_fix_t reply_fix;
static uint16_t reply_ttff;

static void pwrkey(void *hw, bool asserted)
{
    (void)hw;
    gpio_write(MODEM_PWRKEY_PORT, MODEM_PWRKEY_PIN, asserted);  // NPN: high = PWRKEY low
}

static void at_write(void *io, const char *data, size_t len)
{
    uart_write((uart_t *)io, data, len);
}

static void on_rx(void *ctx)
{
    (void)ctx;
    BaseType_t woken = pdFALSE;
    if (self != NULL) {
        xTaskNotifyFromISR(self, EV_RX, eSetBits, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static void reply(bool ok)
{
    reply_ok = ok;
    req_op = OP_NONE;
    (void)xTaskNotifyGiveIndexed(requester, REPLY_INDEX);
}

static void power_up_if_needed(uint32_t now)
{
    if (bg95_is_idle(&modem)) {
        pm_lock(PM_LOCK_MODEM);
        gpio_write(LED_PORT, LED_PIN, true);
        bg95_start(&modem, now);
    }
}

static void modem_task(void *arg)
{
    (void)arg;
    bool issued = false;
    uint32_t op_start = 0U;
    bg95_state_t shown = BG95_OFF;
    for (;;) {
        bool active = !bg95_is_idle(&modem) || (req_op != OP_NONE);
        uint32_t ev = 0U;
        (void)xTaskNotifyWait(0U, UINT32_MAX, &ev, active ? pdMS_TO_TICKS(MODEM_POLL_MS) : portMAX_DELAY);
        uint32_t now = pm_uptime_ms();

        if ((ev & EV_REQ) != 0U) {
            issued = false;
            op_start = now;
            if ((req_op == OP_FIX) || (req_op == OP_SEND)) {
                power_up_if_needed(now);
            } else if (!bg95_power_off(&modem)) {
                reply(true);  // already off
            } else {
                // shutting down
            }
        }

        uint8_t buf[64];
        size_t n;
        while ((n = uart_read(&uart_modem, buf, sizeof buf)) > 0U) {
            bg95_feed(&modem, buf, n);
        }
        bg95_poll(&modem, now);

        bg95_state_t st = bg95_state(&modem);
        if (st != shown) {
            console_print("modem: %s -> %s\r\n", bg95_state_name(shown), bg95_state_name(st));
            shown = st;
        }

        switch (req_op) {
        case OP_FIX:
            if (!issued && (st == BG95_READY)) {
                issued = bg95_gnss_start(&modem, req_timeout_ms);
            } else if (issued && (bg95_gnss_status(&modem) != BG95_GNSS_BUSY)) {
                reply_fix = *bg95_gnss_fix(&modem);
                reply_ttff = modem.ttff_s;
                reply(bg95_gnss_status(&modem) == BG95_GNSS_FIX);
            } else if ((st == BG95_FAILED) || (!issued && ((now - op_start) >= MODEM_UP_TIMEOUT_MS))) {
                reply(false);
            } else {
                // in progress
            }
            break;
        case OP_SEND:
            if (!issued && (st == BG95_READY)) {
                issued = bg95_send(&modem, req_data, req_len);
            } else if (issued && (bg95_send_status(&modem) != BG95_SEND_BUSY)) {
                reply(bg95_send_status(&modem) == BG95_SEND_DONE);
            } else if ((st == BG95_FAILED) || (!issued && ((now - op_start) >= MODEM_UP_TIMEOUT_MS))) {
                reply(false);
            } else {
                // in progress
            }
            break;
        case OP_OFF:
            if (st == BG95_OFF) {
                reply(true);
            }
            break;
        case OP_NONE:
        default:
            break;
        }

        if (bg95_is_idle(&modem) && ((pm_locks() & PM_LOCK_MODEM) != 0U)) {
            gpio_write(LED_PORT, LED_PIN, false);
            pm_unlock(PM_LOCK_MODEM);
        }
    }
}

static bool request(op_t op)
{
    requester = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTakeIndexed(REPLY_INDEX, pdTRUE, 0U);  // drop a stale reply
    req_op = op;
    (void)xTaskNotify(self, EV_REQ, eSetBits);
    (void)ulTaskNotifyTakeIndexed(REPLY_INDEX, pdTRUE, portMAX_DELAY);
    return reply_ok;
}

bool modem_get_fix(uint32_t timeout_ms, gnss_fix_t *fix, uint16_t *ttff_s)
{
    req_timeout_ms = timeout_ms;
    bool ok = request(OP_FIX);
    if (ok) {
        *fix = reply_fix;
        *ttff_s = reply_ttff;
    }
    return ok;
}

bool modem_send(const uint8_t *data, size_t len)
{
    if (len > sizeof req_data) {
        return false;
    }
    memcpy(req_data, data, len);
    req_len = len;
    return request(OP_SEND);
}

void modem_power_off(void)
{
    (void)request(OP_OFF);
}

bg95_state_t modem_state(void)
{
    return bg95_state(&modem);
}

void modem_task_start(void)
{
    gpio_clock_enable(MODEM_PWRKEY_PORT);
    gpio_write(MODEM_PWRKEY_PORT, MODEM_PWRKEY_PIN, false);
    gpio_set_mode(MODEM_PWRKEY_PORT, MODEM_PWRKEY_PIN, GPIO_MODE_OUTPUT);
    uart_init(&uart_modem, RCC_PCLK2_HZ, MODEM_BAUD, on_rx, NULL);  // USART1 is on APB2

    at_io_t io = {at_write, &uart_modem};
    bg95_hw_t hw = {pwrkey, NULL};
    bg95_init(&modem, io, hw, &cfg);
    self = xTaskCreateStatic(modem_task, "modem", sizeof stack / sizeof stack[0], NULL, PRIO_MODEM, stack, &tcb);
}
