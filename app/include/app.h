#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "bg95.h"
#include "task.h"

// Task priorities: motion > modem > app > console > idle.
#define PRIO_MOTION 4U
#define PRIO_MODEM 3U
#define PRIO_APP 2U
#define PRIO_CONSOLE 1U

// Event bits on notification index 0 of the app task.
#define EV_MOTION 0x1U
#define EV_REPORT_NOW 0x2U

bool rtos_take_panic(char *what, size_t what_size, char *where, size_t where_size, int *line);

void console_start(void);
void console_print(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void modem_task_start(void);
// Blocking requests from the app task (one at a time). Each powers the modem on if needed.
bool modem_get_fix(uint32_t timeout_ms, gnss_fix_t *fix, uint16_t *ttff_s);
bool modem_send(const uint8_t *data, size_t len);
void modem_power_off(void);
bg95_state_t modem_state(void);

void app_task_start(void);
TaskHandle_t app_task_handle(void);
void app_status(void);

void motion_start(void);
bool motion_present(void);

#endif
