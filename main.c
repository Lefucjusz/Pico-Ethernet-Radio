#include <pico/stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include <stdio.h>
#include <lwip/tcpip.h>
#include <lwip/dhcp.h>
#include <enc28j60.h>

#define LED_PIN 25

extern err_t ethernetif_init(struct netif *netif); // TODO

static void bootstrap_task(void *arg)
{
    static struct netif netif;

    tcpip_init(NULL, NULL);

    netif_add(&netif, NULL, NULL, NULL, NULL, ethernetif_init, tcpip_input);
    netif_set_default(&netif);
    netif_set_up(&netif);

    dhcp_start(&netif);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    while (1) {
        gpio_xor_mask(1 << LED_PIN);
        vTaskDelay(1000);
        printf("UP=%d LINK=%d IP=%s\n",
            netif_is_up(&netif),
            netif_is_link_up(&netif),
            ip4addr_ntoa(netif_ip4_addr(&netif)));
    }
}

int main(void)
{
    stdio_init_all();

    TaskHandle_t t1;

    xTaskCreate(bootstrap_task, "bootstrap", 2048 / sizeof(uint32_t), NULL, 1, &t1);

    vTaskCoreAffinitySet(t1, (1 << 0));

    vTaskStartScheduler();

    /* Unreachable */
    while (1) {}
}
