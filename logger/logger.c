#include "logger.h"
#include <FreeRTOS.h>
#include <semphr.h>
#include <pico/time.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

#define LOGGER_LINE_BUFFER_SIZE 2048

static char line_buffer[LOGGER_LINE_BUFFER_SIZE];
static SemaphoreHandle_t mutex;

static const char *level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

static const char *logger_get_task_name(void)
{
    return pcTaskGetName(xTaskGetCurrentTaskHandle());
}

void logger_init(void)
{
    mutex = xSemaphoreCreateMutex();
}

void logger_log(logger_level_t level, const char *file, int line, const char *function, const char *fmt, ...)
{
    xSemaphoreTake(mutex, portMAX_DELAY);

    size_t buffer_size_left = sizeof(line_buffer);
    size_t buffer_index;
    size_t bytes_written;

    /* Write header */
    const uint32_t timestamp = to_ms_since_boot(get_absolute_time());
    const char *task_name = logger_get_task_name();
    bytes_written = snprintf(line_buffer, buffer_size_left, "%" PRIu32 "ms %-5s [%s] %s:%d:%s(): ", timestamp, level_names[level], task_name, file, line, function);
    if (bytes_written >= buffer_size_left) {
        goto out_error;
    }
    buffer_index = bytes_written;
    buffer_size_left -= bytes_written;

    /* Write content */
    va_list args;
    va_start(args, fmt);
    bytes_written = vsnprintf(&line_buffer[buffer_index], buffer_size_left, fmt, args);
    va_end(args);

    if (bytes_written >= buffer_size_left) {
        goto out_error;
    }

    printf("%s\n", line_buffer);

out_error:
    xSemaphoreGive(mutex);
}
