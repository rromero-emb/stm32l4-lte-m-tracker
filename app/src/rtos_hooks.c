// What happens when the RTOS detects a bug: record it in SRAM2 (.noinit) and reset, like a fault.
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "stm32l4xx.h"
#include "task.h"

#define PANIC_MAGIC 0x9A41C000U

typedef struct {
    uint32_t magic;
    char what[16];
    char where[40];
    int line;
} panic_record_t;

__attribute__((section(".noinit"))) static panic_record_t panic_rec;

static void copy(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (n >= size) {
        src += n - (size - 1U);  // keep the end of long paths: the file name
        n = size - 1U;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

__attribute__((noreturn)) void rtos_panic(const char *what, const char *file, int line)
{
    __disable_irq();
    copy(panic_rec.what, sizeof panic_rec.what, what);
    copy(panic_rec.where, sizeof panic_rec.where, file);
    panic_rec.line = line;
    panic_rec.magic = PANIC_MAGIC;
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) {
        __BKPT(0);
    }
    NVIC_SystemReset();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    rtos_panic("stack overflow", name, 0);
}

// Returns true once with the record of the previous boot.
bool rtos_take_panic(char *what, size_t what_size, char *where, size_t where_size, int *line)
{
    if (panic_rec.magic != PANIC_MAGIC) {
        return false;
    }
    copy(what, what_size, panic_rec.what);
    copy(where, where_size, panic_rec.where);
    *line = panic_rec.line;
    panic_rec.magic = 0U;
    return true;
}
