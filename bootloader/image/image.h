#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IMAGE_MAGIC 0xCAFED00D

typedef struct
{
    uint32_t magic;
    uint32_t image_size;
    uint32_t crc32;
    uint8_t reserved[244];  // Pad to 256 bytes to satisfy VTOR value requirements
} image_header_t;

bool image_validate_active(void);
bool image_validate_staging(void);

const image_header_t *image_get_header_active(void);
const image_header_t *image_get_header_staging(void);

__attribute__((noreturn)) void image_boot(void);
