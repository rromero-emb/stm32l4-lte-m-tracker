#ifndef FAULT_H
#define FAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Crash capture: HardFault/MemManage/BusFault/UsageFault save the stacked registers and the
// fault status registers to SRAM2 (.noinit, survives reset), then reset the MCU.
// On the next boot fault_take_last() returns the record so it can be printed or sent.

#define FAULT_MAGIC 0xFA017EC0U

typedef struct {
    uint32_t magic;
    uint32_t exception;  // IPSR: 3 = HardFault, 4 = MemManage, 5 = BusFault, 6 = UsageFault
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
    uint32_t cfsr, hfsr, mmfar, bfar;
} fault_record_t;

// Turns on the configurable fault handlers and the divide-by-zero trap.
void fault_init(void);
// Copies the record from the previous crash (if any) and clears it. Returns false if none.
bool fault_take_last(fault_record_t *out);

// Pure helpers (also built and tested on the host).
const char *fault_exception_name(uint32_t exception);
// Writes a comma-separated list of the CFSR/HFSR bits that are set, e.g. "DIVBYZERO, FORCED".
size_t fault_describe(uint32_t cfsr, uint32_t hfsr, char *buf, size_t size);

#endif
