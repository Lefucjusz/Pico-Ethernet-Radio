#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

bool fota_begin(size_t image_size);
bool fota_write_chunk(const uint8_t *data, size_t size);
void fota_finalize(void);
