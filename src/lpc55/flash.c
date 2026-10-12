// Flash (IAP) functionality for LPC55xx using the boot ROM flash API
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "autoconf.h" // CONFIG_BLOCK_SIZE
#include "board/irq.h" // irq_disable
#include "canboot.h" // board_flash_read
#include "flash.h" // flash_write_block

#define PAGE_SIZE 512
#define FLASH_ERASE_KEY 0x6b65666c // "lfek"

// Layout of flash_config_t from the NXP SDK (fsl_iap.h)
struct rom_flash_config {
    uint32_t block_base, total_size, block_count, page_size, sector_size;
    uint32_t ffr_config[5];
    uint32_t sys_freq_mhz;
    uint32_t mode_config[4];
};

// Flash part of the ROM API tree (identical in versions 0 and 1)
struct rom_flash_driver {
    uint32_t version;
    int32_t (*init)(struct rom_flash_config *config);
    int32_t (*erase)(struct rom_flash_config *config, uint32_t start
                     , uint32_t len, uint32_t key);
    int32_t (*program)(struct rom_flash_config *config, uint32_t start
                       , const uint8_t *src, uint32_t len);
    int32_t (*verify_erase)(struct rom_flash_config *config, uint32_t start
                            , uint32_t len);
};

struct rom_api_tree {
    void (*run_bootloader)(void *arg);
    uint32_t bootloader_version;
    const char *copyright;
    uint32_t reserved0;
    const struct rom_flash_driver *flash;
};

#define ROM_API ((const struct rom_api_tree *)0x130010f0)
#define ROM_VERSION_MAJOR(v) (((v) >> 16) & 0xff)

static struct rom_flash_config flash_config;
static uint8_t flash_ready;

// Initialize the ROM flash driver on first use
static int
rom_flash_init(void)
{
    if (flash_ready)
        return 0;
    // Only the version 3 ROM (all LPC5528 silicon) is supported; older
    // LPC55S69 rev 0A parts use a different entry for erase and program
    if (ROM_VERSION_MAJOR(ROM_API->bootloader_version) != 3)
        return -1;
    flash_config.sys_freq_mhz = CONFIG_CLOCK_FREQ / 1000000;
    irq_disable();
    int32_t ret = ROM_API->flash->init(&flash_config);
    irq_enable();
    if (ret)
        return -1;
    flash_ready = 1;
    return 0;
}

// Check if a flash page is erased. Reading an erased (never programmed)
// page raises an ECC bus fault, so pages must be checked before reading.
static int
page_is_erased(uint32_t page_address)
{
    if (rom_flash_init() < 0)
        return 1;
    irq_disable();
    int32_t ret = ROM_API->flash->verify_erase(&flash_config, page_address
                                               , PAGE_SIZE);
    irq_enable();
    return ret == 0;
}

// Read a block of flash, returning 0xff for erased pages
void
board_flash_read(uint32_t address, uint32_t *dest)
{
    if (page_is_erased(address & ~(PAGE_SIZE - 1)))
        memset(dest, 0xff, CONFIG_BLOCK_SIZE);
    else
        memcpy(dest, (void*)address, CONFIG_BLOCK_SIZE);
}

// Erase and program one page
static int
write_page(uint32_t page_address, uint8_t *data)
{
    if (rom_flash_init() < 0)
        return -4;
    irq_disable();
    int32_t ret = ROM_API->flash->erase(&flash_config, page_address
                                        , PAGE_SIZE, FLASH_ERASE_KEY);
    if (!ret)
        ret = ROM_API->flash->program(&flash_config, page_address
                                      , data, PAGE_SIZE);
    irq_enable();
    if (ret)
        return -5;
    if (memcmp(data, (void*)page_address, PAGE_SIZE) != 0)
        return -3;
    return 0;
}

// Buffer to consolidate block writes into whole page writes
static uint8_t page_buf[PAGE_SIZE] __aligned(4);
static uint32_t page_buf_address, page_buf_valid;
static uint32_t page_write_count;

static int
flush_page(void)
{
    if (!page_buf_valid)
        return 0;
    page_buf_valid = 0;
    int ret = write_page(page_buf_address, page_buf);
    if (ret < 0)
        return ret;
    page_write_count++;
    return 0;
}

int
flash_write_block(uint32_t block_address, uint32_t *data)
{
    if (block_address & (CONFIG_BLOCK_SIZE - 1))
        // Not a block aligned address
        return -1;
    if (block_address + CONFIG_BLOCK_SIZE > CONFIG_FLASH_START
                                            + CONFIG_FLASH_SIZE)
        return -2;
    uint32_t page_address = block_address & ~(PAGE_SIZE - 1);
    if (!page_buf_valid || page_address != page_buf_address) {
        int ret = flush_page();
        if (ret < 0)
            return ret;
        memset(page_buf, 0xff, PAGE_SIZE);
        page_buf_address = page_address;
        page_buf_valid = 1;
    }
    memcpy(&page_buf[block_address - page_address], data, CONFIG_BLOCK_SIZE);
    return 0;
}

int
flash_complete(void)
{
    int ret = flush_page();
    if (ret < 0)
        return ret;
    return page_write_count;
}
