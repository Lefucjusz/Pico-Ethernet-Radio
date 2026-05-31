#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void enc28j60_init(const uint8_t *mac_addr);

void enc28j60_send_packet(const uint8_t *packet, size_t size);
size_t enc28j60_receive_packet(uint8_t *packet, size_t max_size);

bool enc28j60_packets_pending(void);

uint8_t enc28j60_get_revision(void);

// TODO add Rx packets pending, link up, link down