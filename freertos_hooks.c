#include <FreeRTOS.h>
#include <task.h>
#include <stdlib.h>

#if configUSE_MALLOC_FAILED_HOOK == 1
void vApplicationMallocFailedHook(void)
{
    abort();
}
#endif

#if configCHECK_FOR_STACK_OVERFLOW > 0
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    abort();
}
#endif
