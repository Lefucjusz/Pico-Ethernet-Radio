#include <pico/stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>

static void blink_task(void *arg)
{
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);

    while (1) {
        gpio_xor_mask(1 << 25);
        printf("%s: running on core: %d\n", __func__, get_core_num());
        vTaskDelay(1000);
    }
}

static void print_task(void *arg)
{
    int cnt = 0;

    while (1) {
        printf("%s: counter value: %d, running on core: %d\n", __func__, cnt, get_core_num());
        vTaskDelay(200);
        ++cnt;
    }
}

int main(void)
{
    stdio_init_all();

    TaskHandle_t t1, t2;

    xTaskCreate(blink_task, "blink_task", 1024 / sizeof(uint32_t), NULL, 1, &t1);
    xTaskCreate(print_task, "print_task", 1024 / sizeof(uint32_t), NULL, 2, &t2);

    vTaskCoreAffinitySet(t1, (1 << 0));
    vTaskCoreAffinitySet(t2, (1 << 1));

    vTaskStartScheduler();

    /* Unreachable */
    while (1) {}
}
