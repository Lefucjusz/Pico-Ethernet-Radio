#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>

#if configUSE_MALLOC_FAILED_HOOK == 1
void vApplicationMallocFailedHook(void)
{
    printf("PANIC: %s\n", __func__);
    configASSERT(0);
}
#endif

#if configCHECK_FOR_STACK_OVERFLOW > 0
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("PANIC: %s: %s", __func__, pcTaskName);
    configASSERT(0);
}
#endif
