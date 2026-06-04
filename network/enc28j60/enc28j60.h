#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef void (*enc28j60_irq_callback_t)(void);

void enc28j60_init(const uint8_t *mac_addr);

void enc28j60_set_irq_callback(enc28j60_irq_callback_t callback);
void enc28j60_enable_interrupts(bool enable);
uint8_t enc28j60_get_irq_flags(void);

void enc28j60_send_packet(const uint8_t *packet, size_t size);
size_t enc28j60_receive_packet(uint8_t *packet, size_t max_size);

uint8_t enc28j60_get_rx_packets_count(void);
bool enc28j60_is_link_up(void);

uint8_t enc28j60_get_revision(void);
