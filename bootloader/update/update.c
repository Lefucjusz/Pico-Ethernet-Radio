#include "update.h"
#include <hardware/sync.h>
#include <pico/flash.h>
#include <image.h>
#include <common/flash_layout.h>
#include <common/update_status.h>
#include <stdio.h>
#include <string.h>

#define UPDATE_ALIGN_TO_SECTOR(val) (((val) + FLASH_LAYOUT_SECTOR_SIZE - 1) & ~(FLASH_LAYOUT_SECTOR_SIZE - 1))
#define UPDATE_BYTES_TO_PAGES(val) (((val) + FLASH_LAYOUT_PAGE_SIZE - 1) / FLASH_LAYOUT_PAGE_SIZE)

bool update_check_pending(void)
{
    const uint8_t *config = (uint8_t *)(FLASH_LAYOUT_BASE + FLASH_LAYOUT_CONFIG_AREA_OFFSET);

    return (config[FLASH_LAYOUT_CONFIG_STATUS_OFFSET] == UPDATE_STATUS_UPDATE_PENDING);
}

bool update_apply(void)
{
    bool status;

    /* Disable interrupts before accessing flash */
    const uint32_t primask = save_and_disable_interrupts();

    /* Validate image */
    if (!image_validate_staging()) {
        printf("Staging image validation failed!\n");
        status = false;
        goto out_error;
    }

    /* Get image size with header */
    const image_header_t *header = image_get_header_staging();
    const size_t total_size = header->image_size + sizeof(image_header_t);

    printf("Size:\t%zuB\n", header->image_size);
    printf("CRC32:\t0x%08X\n", header->crc32);

    /* Erase current active image */
    const size_t bytes_to_erase = UPDATE_ALIGN_TO_SECTOR(total_size);
    flash_range_erase(FLASH_LAYOUT_ACTIVE_AREA_OFFSET, bytes_to_erase);

    /* Copy image from staging to active */
    uint8_t page_buffer[FLASH_LAYOUT_PAGE_SIZE];
    const uint8_t *image = (uint8_t *)header; // Header is at the beginning of the image
    const size_t pages_to_copy = UPDATE_BYTES_TO_PAGES(total_size);
    for (size_t i = 0; i < pages_to_copy; ++i) {
        const size_t offset_bytes = i * FLASH_LAYOUT_PAGE_SIZE;
        memcpy(page_buffer, &image[offset_bytes], FLASH_LAYOUT_PAGE_SIZE);
        flash_range_program(FLASH_LAYOUT_ACTIVE_AREA_OFFSET + offset_bytes, page_buffer, FLASH_LAYOUT_PAGE_SIZE);
    }

    status = true;

out_error:
    /* Clear update pending status */
    flash_range_erase(FLASH_LAYOUT_CONFIG_AREA_OFFSET, FLASH_LAYOUT_CONFIG_AREA_SIZE);

    /* Re-enable interrupts */
    restore_interrupts(primask);

    return status;
}
