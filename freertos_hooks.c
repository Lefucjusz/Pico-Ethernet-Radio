#include <FreeRTOS.h>
#include <task.h>
#include <pico.h>

#if configUSE_MALLOC_FAILED_HOOK == 1
void vApplicationMallocFailedHook(void)
{
    panic("%s", __func__);
}
#endif

#if configCHECK_FOR_STACK_OVERFLOW > 0
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    panic("%s: %s", __func__, pcTaskName);
}
#endif
