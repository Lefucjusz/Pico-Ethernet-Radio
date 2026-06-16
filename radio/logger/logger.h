#pragma once

#include <string.h>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

typedef enum
{
    LOGGER_LEVEL_DEBUG,
    LOGGER_LEVEL_INFO,
    LOGGER_LEVEL_WARN,
    LOGGER_LEVEL_ERROR,
    LOGGER_LEVEL_FATAL,
} logger_level_t;

void logger_init(void);
void logger_log(logger_level_t level, const char *file, int line, const char *function, const char *fmt, ...) __attribute__((format(printf, 5, 6)));

#define LOG_DEBUG(...) do { logger_log(LOGGER_LEVEL_DEBUG, __FILENAME__, __LINE__, __func__, __VA_ARGS__); } while (0)
#define LOG_INFO(...) do { logger_log(LOGGER_LEVEL_INFO, __FILENAME__, __LINE__, __func__, __VA_ARGS__); } while (0)
#define LOG_WARN(...) do { logger_log(LOGGER_LEVEL_WARN, __FILENAME__, __LINE__, __func__, __VA_ARGS__); } while (0)
#define LOG_ERROR(...) do { logger_log(LOGGER_LEVEL_ERROR, __FILENAME__, __LINE__, __func__, __VA_ARGS__); } while (0)
#define LOG_FATAL(...) do { logger_log(LOGGER_LEVEL_FATAL, __FILENAME__, __LINE__, __func__, __VA_ARGS__); } while (0)
