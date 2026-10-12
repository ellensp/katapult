// Hardware interface to the full-speed USB0 device controller on lpc55
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "board/armcm_boot.h" // armcm_enable_irq
#include "board/usb_cdc_ep.h" // USB_CDC_EP_BULK_IN
#include "command.h" // DECL_CONSTANT_STR
#include "generic/usb_cdc.h" // usb_notify_ep0
#include "internal.h" // USB0
#include "sched.h" // DECL_INIT

// The endpoint command/status list and the packet buffers are placed in
// the dedicated USB RAM. The list must be 256 byte aligned and each
// buffer 64 byte aligned.
#define USB_RAM FSL_FEATURE_USB_USB_RAM_BASE_ADDRESS
#define EPLIST ((volatile uint32_t *)USB_RAM)
#define BUF_EP0_OUT   (USB_RAM + 0x100)
#define BUF_SETUP     (USB_RAM + 0x140)
#define BUF_EP0_IN    (USB_RAM + 0x180)
#define BUF_BULK_OUT  (USB_RAM + 0x1c0)
#define BUF_BULK_IN   (USB_RAM + 0x200)

// Endpoint command/status list entry bits
#define EP_ACTIVE      (1<<31)
#define EP_DISABLED    (1<<30)
#define EP_STALL       (1<<29)
#define EP_TOGGLE_RST  (1<<28)
#define EP_NBYTES(n)   ((n) << 16)
#define EP_GET_NBYTES(w) (((w) >> 16) & 0x3ff)
#define EP_ADDR(a)     (((a) >> 6) & 0xffff)

// Endpoint command/status list indexes (buffer 0 of each endpoint)
#define EPL_EP0_OUT    0
#define EPL_SETUP      1
#define EPL_EP0_IN     2
#define EPL_OUT(ep)    ((ep) * 4)
#define EPL_IN(ep)     ((ep) * 4 + 2)

// INTSTAT/INTEN bits
#define INT_OUT(ep)    (1 << ((ep) * 2))
#define INT_IN(ep)     (1 << ((ep) * 2 + 1))

// DEVCMDSTAT bits that are cleared by writing a one
#define DEVCMDSTAT_W1C (USB_DEVCMDSTAT_SETUP_MASK | USB_DEVCMDSTAT_DCON_C_MASK \
                        | USB_DEVCMDSTAT_DSUS_C_MASK | USB_DEVCMDSTAT_DRES_C_MASK)

static uint8_t usb_configured, set_address;
static uint32_t bulk_in_flags;

// Update DEVCMDSTAT without clearing pending status change bits
static void
devcmdstat_update(uint32_t clear, uint32_t set)
{
    uint32_t val = USB0->DEVCMDSTAT & ~DEVCMDSTAT_W1C;
    USB0->DEVCMDSTAT = (val & ~clear) | set;
}

static void
ep_arm(uint32_t idx, uint32_t buf, uint32_t len, uint32_t flags)
{
    EPLIST[idx] = EP_ACTIVE | flags | EP_NBYTES(len) | EP_ADDR(buf);
}

// Place all endpoints in a not-active (NAK) state
static void
eplist_reset(void)
{
    int i;
    for (i = 0; i < FSL_FEATURE_USB_EP_NUM * 4; i++)
        EPLIST[i] = 0;
    EPLIST[EPL_SETUP] = EP_ADDR(BUF_SETUP);
}


/****************************************************************
 * Interface
 ****************************************************************/

int_fast8_t
usb_read_bulk_out(void *data, uint_fast8_t max_len)
{
    uint32_t idx = EPL_OUT(USB_CDC_EP_BULK_OUT), epl = EPLIST[idx];
    if (!usb_configured || epl & EP_ACTIVE)
        // No data ready
        return -1;
    uint32_t count = USB_CDC_EP_BULK_OUT_SIZE - EP_GET_NBYTES(epl);
    if (count > max_len)
        count = max_len;
    memcpy(data, (void*)BUF_BULK_OUT, count);
    ep_arm(idx, BUF_BULK_OUT, USB_CDC_EP_BULK_OUT_SIZE, 0);
    return count;
}

int_fast8_t
usb_send_bulk_in(void *data, uint_fast8_t len)
{
    uint32_t idx = EPL_IN(USB_CDC_EP_BULK_IN);
    if (!usb_configured || EPLIST[idx] & EP_ACTIVE)
        // No buffer space available
        return -1;
    memcpy((void*)BUF_BULK_IN, data, len);
    ep_arm(idx, BUF_BULK_IN, len, bulk_in_flags);
    bulk_in_flags = 0;
    return len;
}

int_fast8_t
usb_read_ep0(void *data, uint_fast8_t max_len)
{
    if (USB0->DEVCMDSTAT & USB_DEVCMDSTAT_SETUP_MASK)
        // Transfer interrupted
        return -2;
    uint32_t epl = EPLIST[EPL_EP0_OUT];
    if (epl & EP_ACTIVE)
        // No data ready
        return -1;
    uint32_t count = USB_CDC_EP0_SIZE - EP_GET_NBYTES(epl);
    if (count > max_len)
        count = max_len;
    memcpy(data, (void*)BUF_EP0_OUT, count);
    ep_arm(EPL_EP0_OUT, BUF_EP0_OUT, USB_CDC_EP0_SIZE, 0);
    return count;
}

int_fast8_t
usb_read_ep0_setup(void *data, uint_fast8_t max_len)
{
    if (!(USB0->DEVCMDSTAT & USB_DEVCMDSTAT_SETUP_MASK))
        // No setup packet ready
        return -1;
    uint32_t count = max_len > 8 ? 8 : max_len;
    memcpy(data, (void*)BUF_SETUP, count);
    // Clear any stall, acknowledge the setup packet, and accept the
    // data or status stage that follows it
    EPLIST[EPL_EP0_OUT] = 0;
    EPLIST[EPL_EP0_IN] = 0;
    devcmdstat_update(0, USB_DEVCMDSTAT_SETUP_MASK);
    ep_arm(EPL_EP0_OUT, BUF_EP0_OUT, USB_CDC_EP0_SIZE, 0);
    return count;
}

int_fast8_t
usb_send_ep0(const void *data, uint_fast8_t len)
{
    if (USB0->DEVCMDSTAT & USB_DEVCMDSTAT_SETUP_MASK)
        // Transfer interrupted
        return -2;
    if (EPLIST[EPL_EP0_IN] & EP_ACTIVE)
        // No buffer space available
        return -1;
    memcpy((void*)BUF_EP0_IN, data, len);
    ep_arm(EPL_EP0_IN, BUF_EP0_IN, len, 0);
    return len;
}

void
usb_stall_ep0(void)
{
    if (USB0->DEVCMDSTAT & USB_DEVCMDSTAT_SETUP_MASK) {
        // A new request arrived - process it instead of stalling
        usb_notify_ep0();
        return;
    }
    EPLIST[EPL_EP0_OUT] = EP_STALL;
    EPLIST[EPL_EP0_IN] = EP_STALL;
}

void
usb_set_address(uint_fast8_t addr)
{
    // The new address is applied once the status stage completes
    set_address = addr | 0x80;
    usb_send_ep0(NULL, 0);
}

void
usb_set_configure(void)
{
    EPLIST[EPL_IN(USB_CDC_EP_ACM)] = 0;
    EPLIST[EPL_IN(USB_CDC_EP_BULK_IN)] = 0;
    bulk_in_flags = EP_TOGGLE_RST;
    ep_arm(EPL_OUT(USB_CDC_EP_BULK_OUT), BUF_BULK_OUT
           , USB_CDC_EP_BULK_OUT_SIZE, EP_TOGGLE_RST);
    usb_configured = 1;
}


/****************************************************************
 * Setup and interrupts
 ****************************************************************/

static void
usb_reset(void)
{
    // The hardware returns to address 0 on a bus reset
    usb_configured = set_address = 0;
    eplist_reset();
}

void
USB0_IRQHandler(void)
{
    uint32_t intstat = USB0->INTSTAT;
    USB0->INTSTAT = intstat;
    if (intstat & USB_INTSTAT_DEV_INT_MASK) {
        uint32_t ds = USB0->DEVCMDSTAT;
        if (ds & USB_DEVCMDSTAT_DRES_C_MASK)
            usb_reset();
        devcmdstat_update(0, ds & (USB_DEVCMDSTAT_DCON_C_MASK
                                   | USB_DEVCMDSTAT_DSUS_C_MASK
                                   | USB_DEVCMDSTAT_DRES_C_MASK));
    }
    if (intstat & INT_IN(0) && set_address) {
        // Apply address after the status stage was transmitted
        devcmdstat_update(USB_DEVCMDSTAT_DEV_ADDR_MASK, set_address & 0x7f);
        set_address = 0;
    }
    if (intstat & (INT_OUT(0) | INT_IN(0)))
        usb_notify_ep0();
    if (intstat & INT_OUT(USB_CDC_EP_BULK_OUT))
        usb_notify_bulk_out();
    if (intstat & INT_IN(USB_CDC_EP_BULK_IN))
        usb_notify_bulk_in();
}

// The USB-C port on the MKS Owl does not connect VBUS to the chip. The
// USB0_VBUS function on PIO0_22 with its pull-up enabled makes the
// controller see VBUS as present.
DECL_CONSTANT_STR("RESERVE_PINS_USB", "PIO0_22");

void
usbserial_init(void)
{
    // USB0 clock is FRO_HF (96MHz) / 2, trimmed to the host using SOF
    ANACTRL->FRO192M_CTRL |= (ANACTRL_FRO192M_CTRL_ENA_96MHZCLK_MASK
                              | ANACTRL_FRO192M_CTRL_USBCLKADJ_MASK);
    SYSCON->USB0CLKDIV = 1;
    SYSCON->USB0CLKSEL = 3; // FRO_HF

    // Power up the full-speed PHY
    PMC->PDRUNCFGCLR0 = PMC_PDRUNCFG0_PDEN_USBFSPHY_MASK;

    // Reset the USB0 controller and enable its clocks and the USB RAM
    SYSCON->PRESETCTRLSET[1] = SYSCON_PRESETCTRL1_USB0_DEV_RST_MASK;
    SYSCON->PRESETCTRLSET[2] = (SYSCON_PRESETCTRL2_USB0_HOSTM_RST_MASK
                                | SYSCON_PRESETCTRL2_USB0_HOSTS_RST_MASK);
    SYSCON->PRESETCTRLCLR[1] = SYSCON_PRESETCTRL1_USB0_DEV_RST_MASK;
    SYSCON->PRESETCTRLCLR[2] = (SYSCON_PRESETCTRL2_USB0_HOSTM_RST_MASK
                                | SYSCON_PRESETCTRL2_USB0_HOSTS_RST_MASK);
    SYSCON->AHBCLKCTRLSET[1] = SYSCON_AHBCLKCTRL1_USB0_DEV_MASK;
    SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL2_USB1_RAM_MASK;

    // The port is shared with the host controller; select device mode
    SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL2_USB0_HOSTS_MASK;
    USBFSH->PORTMODE |= USBFSH_PORTMODE_DEV_ENABLE_MASK;
    SYSCON->AHBCLKCTRLCLR[2] = SYSCON_AHBCLKCTRL2_USB0_HOSTS_MASK;

    gpio_peripheral(GPIO(0, 22), 7, 1);

    // Setup the endpoint list and connect
    USB0->EPLISTSTART = USB_RAM;
    USB0->DATABUFSTART = USB_RAM;
    eplist_reset();
    USB0->INTSTAT = 0xffffffff;
    USB0->INTEN = (USB_INTEN_DEV_INT_EN_MASK | INT_OUT(0) | INT_IN(0)
                   | INT_OUT(USB_CDC_EP_BULK_OUT)
                   | INT_IN(USB_CDC_EP_BULK_IN));
    USB0->DEVCMDSTAT = USB_DEVCMDSTAT_DEV_EN_MASK | USB_DEVCMDSTAT_DCON_MASK;
    armcm_enable_irq(USB0_IRQHandler, USB0_IRQn, 1);
}
DECL_INIT(usbserial_init);
