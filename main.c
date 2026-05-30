#include <pico/stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <lwip/tcpip.h>

#define LED_PIN 25

static void bootstrap_task(void *arg)
{
    tcpip_init(NULL, NULL);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    while (1) {
        gpio_xor_mask(1 << LED_PIN);
        vTaskDelay(1000);
    }
}

int main(void)
{
    stdio_init_all();

    TaskHandle_t t1;

    xTaskCreate(bootstrap_task, "bootstrap", 1024 / sizeof(uint32_t), NULL, 1, &t1);

    vTaskCoreAffinitySet(t1, (1 << 0));

    vTaskStartScheduler();

    /* Unreachable */
    while (1) {}
}
