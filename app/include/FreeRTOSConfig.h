#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// FreeRTOS V11.1.0 for the STM32L476 (Cortex-M4F). Static allocation only: no heap, every
// task and queue is sized at link time and shows up in the map file.

#include <stdint.h>

void rtos_panic(const char *what, const char *file, int line);

#define configCPU_CLOCK_HZ 80000000U
#define configTICK_RATE_HZ 1024U  // = LPTIM1 4096 Hz / 4: see power/include/lowpower_calc.h
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE 128U
#define configMAX_TASK_NAME_LEN 8
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 2  // [0] events, [1] replies from the modem task

#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_TIMERS 0
#define configTIMER_TASK_STACK_DEPTH configMINIMAL_STACK_SIZE  // unused, but tasks.c V11.1.0 needs it
#define configUSE_CO_ROUTINES 0
#define configUSE_TRACE_FACILITY 0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configKERNEL_PROVIDED_STATIC_MEMORY 1  // idle task stack and TCB

#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2

// Tickless idle with our own implementation (power/src/pm.c): the tick runs on LPTIM1, which
// keeps counting in Stop 2, instead of SysTick, which stops.
#define configUSE_TICKLESS_IDLE 2
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP 2

// 4 priority bits on the STM32L4. Interrupts at 5..15 may call FreeRTOS "FromISR" functions;
// 0..4 are never masked by the kernel (none are used yet).
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configASSERT(x)                           \
    if ((x) == 0) {                               \
        rtos_panic("assert", __FILE__, __LINE__); \
    }

#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetHandle 1

// The startup vector table names the handlers SVC_Handler / PendSV_Handler.
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler

#endif
