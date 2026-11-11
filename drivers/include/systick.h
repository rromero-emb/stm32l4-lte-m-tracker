#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

// 1 ms tick from the core clock.
void systick_init(uint32_t core_hz);
uint32_t systick_ms(void);
void systick_delay_ms(uint32_t ms);

#endif
