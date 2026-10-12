#ifndef __LPC55_INTERNAL_H
#define __LPC55_INTERNAL_H
// Local definitions for lpc55 code

#include "LPC5528.h"
#include "LPC5528_features.h" // FSL_FEATURE_USB_USB_RAM_BASE_ADDRESS

// The NXP header names the GPIO peripheral "GPIO", which clashes with
// the GPIO() pin macro
#define GPIO_REGS ((GPIO_Type *)GPIO_BASE)
#undef GPIO

#define GPIO(PORT, NUM) ((PORT) * 32 + (NUM))
#define GPIO2PORT(PIN) ((PIN) / 32)
#define GPIO2BIT(PIN) (1<<((PIN) % 32))

void gpio_peripheral(uint32_t gpio, int func, int pull_up);

#endif // internal.h
