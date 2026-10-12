#ifndef __LPC55_FLASH_H
#define __LPC55_FLASH_H

#include <stdint.h>

int flash_write_block(uint32_t block_address, uint32_t *data);
int flash_complete(void);

#endif
