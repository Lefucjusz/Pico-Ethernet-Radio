#pragma once

#include <FreeRTOS.h>

#define UTILS_STACK_BYTES_TO_WORDS(b) (((b) + sizeof(StackType_t) - 1) / sizeof(StackType_t))

#define UTILS_FLOAT_TO_Q15(x) ((int16_t)((x) * 32767.0f))
#define UTILS_Q15_MUL(x, y) ((int16_t)(((int32_t)(x) * (int32_t)(y)) >> 15))

#define UTILS_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define UTILS_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define UTILS_CLAMP(val, min, max) UTILS_MIN(max, UTILS_MAX(val, min))

#define UTILS_ARRAY_COUNT(x) (sizeof(x) / sizeof((x)[0]))
