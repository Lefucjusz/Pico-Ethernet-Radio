#include "fota.h"
#include <FreeRTOS.h>
#include <task.h>
#include <pico/flash.h>
#include <hardware/flash.h>
#include <utils.h>
#include <logger.h>
#include <common/flash_layout.h>
#include <common/update_status.h>

#define FOTA_SAFE_EXECUTE_TIMEOUT_MS 100

typedef struct
{
    uint8_t page_buf[FLASH_LAYOUT_PAGE_SIZE];
    size_t page_fill;
    size_t image_size;
    size_t received_size;
    uintptr_t flash_offset;
} fota_ctx_t;

static fota_ctx_t ctx;

static void __no_inline_not_in_flash_func(fota_erase_sector)(void *arg)
{
    flash_range_erase(ctx.flash_offset, FLASH_LAYOUT_SECTOR_SIZE);
}

static void __no_inline_not_in_flash_func(fota_write_page)(void *arg)
{
    flash_range_program(ctx.flash_offset, ctx.page_buf, FLASH_LAYOUT_PAGE_SIZE);
}

static void fota_commit_page(void)
{
    /* Erase next sector if needed */
    if ((ctx.flash_offset % FLASH_LAYOUT_SECTOR_SIZE) == 0) {
        flash_safe_execute(fota_erase_sector, NULL, FOTA_SAFE_EXECUTE_TIMEOUT_MS);
    }

    /* Flush page buffer */
    flash_safe_execute(fota_write_page, NULL, FOTA_SAFE_EXECUTE_TIMEOUT_MS);
    ctx.flash_offset += FLASH_LAYOUT_PAGE_SIZE;
    ctx.page_fill = 0;

    /* Each flash_safe_execute call creates and deletes a new task. Give idle task 
     * some time to clean up, otherwise we're gonna run out of heap. */
    vTaskDelay(pdMS_TO_TICKS(1));
}

static void fota_set_update_pending(void)
{
    /* First byte in config area stores update status */
    ctx.page_buf[0] = UPDATE_STATUS_UPDATE_PENDING;
    memset(&ctx.page_buf[1], 0xFF, FLASH_LAYOUT_PAGE_SIZE - 1);

    ctx.flash_offset = FLASH_LAYOUT_CONFIG_AREA_OFFSET;
    fota_commit_page();
}

bool fota_begin(size_t image_size)
{
    if (image_size > FLASH_LAYOUT_ACTIVE_AREA_SIZE) {
        LOG_ERROR("Image too big (%zuB)", image_size);
        return false;
    }

    ctx.page_fill = 0;
    ctx.image_size = image_size;
    ctx.received_size = 0;
    ctx.flash_offset = FLASH_LAYOUT_STAGING_AREA_OFFSET;

    LOG_INFO("FOTA started");

    return true;
}

bool fota_write_chunk(const uint8_t *data, size_t size)
{
    if ((ctx.received_size + size) > ctx.image_size) {
        LOG_ERROR("Declared image size exceeded");
        return false;
    }

    size_t bytes_written = 0;
    while (bytes_written < size) {
        const size_t page_bytes_left = FLASH_LAYOUT_PAGE_SIZE - ctx.page_fill;
        const size_t data_bytes_left = size - bytes_written;
        const size_t bytes_to_write = UTILS_MIN(data_bytes_left, page_bytes_left);

        /* Copy next chunk to buffer */
        memcpy(&ctx.page_buf[ctx.page_fill], &data[bytes_written], bytes_to_write);
        bytes_written += bytes_to_write;
        ctx.page_fill += bytes_to_write;
        ctx.received_size += bytes_to_write;

        /* Commit page if needed */
        if (ctx.page_fill == FLASH_LAYOUT_PAGE_SIZE) {
            fota_commit_page();
        }
    }

    return true;
}

void fota_finalize(void)
{
    if (ctx.received_size != ctx.image_size) {
        LOG_WARN("Finalizing before receiving the declared data size");
    }

    if (ctx.page_fill > 0) {
        const size_t bytes_empty = FLASH_LAYOUT_PAGE_SIZE - ctx.page_fill;
        memset(&ctx.page_buf[ctx.page_fill], 0xFF, bytes_empty);
        fota_commit_page();
    }

    fota_set_update_pending();

    LOG_INFO("FOTA done!");
}
