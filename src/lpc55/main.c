// Main starting point for LPC55xx boards.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_CLOCK_FREQ
#include "board/armcm_boot.h" // armcm_main
#include "internal.h" // SYSCON
#include "sched.h" // sched_main

#define FREQ_FRO_HF 96000000
#define FLASH_CMD_SET_READ_MODE 0x2

// Set the number of flash wait states in both the FMC and flash controller
static void
flash_set_wait_states(uint32_t ws)
{
    uint32_t prefetch = SYSCON->FMCCR & SYSCON_FMCCR_PREFEN_MASK;
    SYSCON->FMCCR &= ~SYSCON_FMCCR_PREFEN_MASK;
    FLASH->INT_CLR_STATUS = 0x1f;
    FLASH->DATAW[0] = (FLASH->DATAW[0] & ~0x0f) | ws;
    FLASH->CMD = FLASH_CMD_SET_READ_MODE;
    while (!(FLASH->INT_STATUS & FLASH_INT_STATUS_DONE_MASK))
        ;
    SYSCON->FMCCR = ((SYSCON->FMCCR & ~SYSCON_FMCCR_FLASHTIM_MASK)
                     | (ws << SYSCON_FMCCR_FLASHTIM_SHIFT) | prefetch);
}

// Run the core from FRO_HF (96MHz) divided down to CONFIG_CLOCK_FREQ
static void
clock_setup(void)
{
    // Enable the analog control block and the FRO 96MHz output
    SYSCON->PRESETCTRLCLR[2] = SYSCON_PRESETCTRL2_ANALOG_CTRL_RST_MASK;
    SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL2_ANALOG_CTRL_MASK;
    ANACTRL->FRO192M_CTRL |= ANACTRL_FRO192M_CTRL_ENA_96MHZCLK_MASK;

    // Use wait states that are safe up to 100MHz; the clock state left
    // by the boot ROM is not known here
    flash_set_wait_states(8);

    SYSCON->AHBCLKDIV = FREQ_FRO_HF / CONFIG_CLOCK_FREQ - 1;
    SYSCON->MAINCLKSELA = 3; // FRO_HF
    SYSCON->MAINCLKSELB = 0; // main_clk_a
}

// Main entry point - called from armcm_boot.c:ResetHandler()
void
armcm_main(void)
{
    clock_setup();
    sched_main();
}
