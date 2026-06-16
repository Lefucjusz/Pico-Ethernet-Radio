#include "image.h"
#include <hardware/sync.h>
#include <hardware/structs/scb.h>
#include <crc32.h>
#include <common/flash_layout.h>
#include <stdio.h>

static bool image_validate(uintptr_t area_addr)
{
    const image_header_t *header = (image_header_t *)area_addr;
    const uint8_t *image = (uint8_t *)area_addr + sizeof(image_header_t);

    /* Validate magic */
    if (header->magic != IMAGE_MAGIC) {
        printf("Image header mismatch, got 0x%08X, expected 0x%08X\n", header->magic, IMAGE_MAGIC);
        return false;
    }

    /* Validate size */
    if ((header->image_size + sizeof(image_header_t)) > FLASH_LAYOUT_ACTIVE_AREA_SIZE) {
        printf("Image too big or invalid, %uB > %uB\n", header->image_size, FLASH_LAYOUT_ACTIVE_AREA_SIZE);
        return false;
    }

    /* Validate CRC */
    const uint32_t crc = crc32(image, header->image_size, 0);
    if (header->crc32 != crc) {
        printf("CRC32 mismatch, computed 0x%08X, expected 0x%08X\n", crc, header->crc32);
        return false;
    }

    printf("Image valid\n");

    return true;
}

const image_header_t *image_get_header_active(void)
{
    return (image_header_t *)(FLASH_LAYOUT_BASE + FLASH_LAYOUT_ACTIVE_AREA_OFFSET);
}

const image_header_t *image_get_header_staging(void)
{
    return (image_header_t *)(FLASH_LAYOUT_BASE + FLASH_LAYOUT_STAGING_AREA_OFFSET);
}

bool image_validate_active(void)
{
    return image_validate(FLASH_LAYOUT_BASE + FLASH_LAYOUT_ACTIVE_AREA_OFFSET);
}

bool image_validate_staging(void)
{
    return image_validate(FLASH_LAYOUT_BASE + FLASH_LAYOUT_STAGING_AREA_OFFSET);
}

__attribute__((noreturn)) void image_boot(void)
{
    const uint32_t *vectors = (uint32_t *)(FLASH_LAYOUT_BASE + FLASH_LAYOUT_ACTIVE_AREA_OFFSET + sizeof(image_header_t));
    const uint32_t sp = vectors[0];
    const uint32_t reset = vectors[1];
    
    disable_interrupts();

    /* Set VTOR */
    scb_hw->vtor = (uint32_t)vectors;
    __dsb();
    __isb();

    /* Set MSP */
    __asm__ __volatile__("msr msp, %0" : : "r" (sp) : );

    /* Jump to main app */
    ((void (*)(void))reset)();

    /* Unreachable */
    while (1) {}
}
