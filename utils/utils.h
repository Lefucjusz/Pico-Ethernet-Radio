#pragma once

#include <FreeRTOS.h>

#define UTILS_STACK_BYTES_TO_WORDS(b) (((b) + sizeof(StackType_t) - 1) / sizeof(StackType_t))
