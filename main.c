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

    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gateway;

    IP4_ADDR(&ipaddr,  192,168,1,70);
    IP4_ADDR(&netmask, 255,255,255,0);
    IP4_ADDR(&gateway, 192,168,1,1);

    netif_add(&netif, &ipaddr, &netmask, &gateway, NULL, ethernetif_init, tcpip_input);

    netif_set_default(&netif);
    netif_set_up(&netif);
    netif_set_link_up(&netif);

    // printf("Initialized, revision %d\n", enc28j60_get_revision());

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

    xTaskCreate(bootstrap_task, "bootstrap", 2048 / sizeof(uint32_t), NULL, 1, &t1);

    vTaskCoreAffinitySet(t1, (1 << 0));

    vTaskStartScheduler();

    /* Unreachable */
    while (1) {}
}
