// Host-side stand-in for CMSIS core_cm4.h.
// The ST device header (stm32l476xx.h) only needs the access qualifiers and a few
// configuration macros from it to declare the peripheral register structs, which is all
// the unit tests use: drivers receive a pointer to a register block that lives in host RAM.
#ifndef HOST_STUB_CORE_CM4_H
#define HOST_STUB_CORE_CM4_H

#include <stdint.h>

#define __I volatile const
#define __O volatile
#define __IO volatile
#define __IM volatile const
#define __OM volatile
#define __IOM volatile

#endif
