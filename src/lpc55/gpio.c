// GPIO functions on lpc55
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // ffs
#include "board/irq.h" // irq_save
#include "command.h" // shutdown
#include "gpio.h" // gpio_out_setup
#include "internal.h" // gpio_peripheral
#include "sched.h" // sched_shutdown


/****************************************************************
 * Pin mappings
 ****************************************************************/

DECL_ENUMERATION_RANGE("pin", "PIO0_0", GPIO(0, 0), 32);
DECL_ENUMERATION_RANGE("pin", "PIO1_0", GPIO(1, 0), 32);

#define NUM_PORTS 2

// Set the function and pull-up/pull-down mode of a pin
void
gpio_peripheral(uint32_t gpio, int func, int pull_up)
{
    SYSCON->AHBCLKCTRLSET[0] = SYSCON_AHBCLKCTRL0_IOCON_MASK;
    // IOCON mode: 0 = no pull, 1 = pull-down, 2 = pull-up
    uint32_t mode = pull_up ? (pull_up > 0 ? 2 : 1) : 0;
    IOCON->PIO[GPIO2PORT(gpio)][gpio % 32] = (IOCON_PIO_FUNC(func)
                                              | IOCON_PIO_MODE(mode)
                                              | IOCON_PIO_DIGIMODE(1));
}

// Enable the clock for a GPIO port
static void
gpio_clock_enable(uint32_t port)
{
    SYSCON->AHBCLKCTRLSET[0] = (port ? SYSCON_AHBCLKCTRL0_GPIO1_MASK
                                : SYSCON_AHBCLKCTRL0_GPIO0_MASK);
}


/****************************************************************
 * General Purpose Input Output (GPIO) pins
 ****************************************************************/

struct gpio_out
gpio_out_setup(uint8_t pin, uint8_t val)
{
    if (GPIO2PORT(pin) >= NUM_PORTS)
        goto fail;
    struct gpio_out g = { .port=GPIO2PORT(pin), .bit=GPIO2BIT(pin) };
    gpio_clock_enable(g.port);
    gpio_out_reset(g, val);
    return g;
fail:
    shutdown("Not an output pin");
}

void
gpio_out_reset(struct gpio_out g, uint8_t val)
{
    irqstatus_t flag = irq_save();
    gpio_out_write(g, val);
    GPIO_REGS->DIRSET[g.port] = g.bit;
    gpio_peripheral(GPIO(g.port, ffs(g.bit) - 1), 0, 0);
    irq_restore(flag);
}

void
gpio_out_toggle_noirq(struct gpio_out g)
{
    GPIO_REGS->NOT[g.port] = g.bit;
}

void
gpio_out_toggle(struct gpio_out g)
{
    gpio_out_toggle_noirq(g);
}

void
gpio_out_write(struct gpio_out g, uint8_t val)
{
    if (val)
        GPIO_REGS->SET[g.port] = g.bit;
    else
        GPIO_REGS->CLR[g.port] = g.bit;
}


struct gpio_in
gpio_in_setup(uint8_t pin, int8_t pull_up)
{
    if (GPIO2PORT(pin) >= NUM_PORTS)
        goto fail;
    struct gpio_in g = { .port=GPIO2PORT(pin), .bit=GPIO2BIT(pin) };
    gpio_clock_enable(g.port);
    gpio_in_reset(g, pull_up);
    return g;
fail:
    shutdown("Not an input pin");
}

void
gpio_in_reset(struct gpio_in g, int8_t pull_up)
{
    irqstatus_t flag = irq_save();
    gpio_peripheral(GPIO(g.port, ffs(g.bit) - 1), 0, pull_up);
    GPIO_REGS->DIRCLR[g.port] = g.bit;
    irq_restore(flag);
}

uint8_t
gpio_in_read(struct gpio_in g)
{
    return !!(GPIO_REGS->PIN[g.port] & g.bit);
}
