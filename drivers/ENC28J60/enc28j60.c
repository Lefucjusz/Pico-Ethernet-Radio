#include "enc28j60.h"
#include "enc28j60_defs.h"
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <pico/time.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <stdio.h>

// TODO add IRQ?

#define ENC28J60_SPI_PORT       spi1
#define ENC28J60_SPI_BAUD_RATE  (4 * 1000 * 1000)

#define ENC28J60_SPI_MOSI_PIN   11
#define ENC28J60_SPI_MISO_PIN   12
#define ENC28J60_SPI_SCK_PIN    10
#define ENC28J60_SPI_CS_PIN     13

/* Rx buf at 0x0000 is needed for errata Rev. B7, Issue 3 */
#define ENC28J60_TX_BUF_SIZE    0x600
#define ENC28J60_RX_BUF_START   ENC28J60_RAM_START
#define ENC28J60_RX_BUF_END     (ENC28J60_RAM_END - ENC28J60_TX_BUF_SIZE - 1)
#define ENC28J60_TX_BUF_START   (ENC28J60_RX_BUF_END + 1)
#define ENC28J60_TX_BUF_END     ENC28J60_RAM_END

#define ENC28J60_MAX_FRAME_LEN  1500

static uint8_t current_bank;
static uint16_t next_packet_ptr;
static SemaphoreHandle_t spi_mutex;

inline static void enc28j60_select(void)
{
    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    gpio_put(ENC28J60_SPI_CS_PIN, false);
}

inline static void enc28j60_deselect(void)
{
    gpio_put(ENC28J60_SPI_CS_PIN, true);
    xSemaphoreGive(spi_mutex);
}

inline static void enc28j60_write(const uint8_t *data, size_t size)
{
    spi_write_blocking(ENC28J60_SPI_PORT, data, size);
}

inline static void enc28j60_write_byte(uint8_t val)
{
    spi_write_blocking(ENC28J60_SPI_PORT, &val, sizeof(val));
}

inline static void enc28j60_read(uint8_t *data, size_t size)
{
    spi_read_blocking(ENC28J60_SPI_PORT, 0xFF, data, size);
}

inline static uint8_t enc28j60_read_byte(void)
{
    uint8_t val;
    spi_read_blocking(ENC28J60_SPI_PORT, 0xFF, &val, sizeof(val));
    return val;
}

static void enc28j60_write_command(uint8_t cmd, uint8_t reg, uint8_t data)
{
    enc28j60_select();
    enc28j60_write_byte(cmd | (reg & ENC28J60_OP_ARG_MASK));
    enc28j60_write_byte(data);
    enc28j60_deselect();
}

static uint8_t enc28j60_read_command(uint8_t cmd, uint8_t reg)
{
    uint8_t data;

    enc28j60_select();
    enc28j60_write_byte(cmd | (reg & ENC28J60_OP_ARG_MASK));
    data = enc28j60_read_byte();
    if ((reg & ENC28J60_TYPE_MAC_MII) != 0) {
        data = enc28j60_read_byte(); // If MAC/MII type, first read is dummy
    }
    enc28j60_deselect();

    return data;
}

static void enc28j60_select_bank(const uint8_t reg)
{
    if (current_bank != (reg & ENC28J60_BANK_FIELD_MASK)) {
        enc28j60_write_command(ENC28J60_BIT_FIELD_CLEAR, ENC28J60_ECON1, ENC28J60_ECON1_BSEL0 | ENC28J60_ECON1_BSEL1);
        enc28j60_write_command(ENC28J60_BIT_FIELD_SET, ENC28J60_ECON1, (reg & ENC28J60_BANK_FIELD_MASK) >> ENC28J60_BANK_SHIFT);
        current_bank = reg & ENC28J60_BANK_FIELD_MASK;
    }
}

static void enc28j60_bit_field_set(uint8_t reg, uint8_t val)
{
    enc28j60_select_bank(reg);
    enc28j60_write_command(ENC28J60_BIT_FIELD_SET, reg, val);
}

static void enc28j60_bit_field_clear(uint8_t reg, uint8_t val)
{
    enc28j60_select_bank(reg);
    enc28j60_write_command(ENC28J60_BIT_FIELD_CLEAR, reg, val);
}

static void enc28j60_write_ctrl_reg(uint8_t reg, uint8_t val)
{
    enc28j60_select_bank(reg);
    enc28j60_write_command(ENC28J60_WRITE_CONTROL_REG, reg, val);
}

static void enc28j60_write_ctrl_reg16(uint8_t reg, uint16_t val)
{
    enc28j60_write_ctrl_reg(reg, val & 0xFF);
    enc28j60_write_ctrl_reg(reg + 1, (val >> 8) & 0xFF);
}

static uint8_t enc28j60_read_ctrl_reg(uint8_t reg)
{
    enc28j60_select_bank(reg);
    return enc28j60_read_command(ENC28J60_READ_CONTROL_REG, reg);
}

static void enc28j60_write_phy_reg(uint8_t reg, uint16_t val)
{
    /* PHY registers are accessed indirectly. First write address of the PHY register to access to MIREGADR. */
    enc28j60_write_ctrl_reg(ENC28J60_MIREGADR, reg);

    /* Then write the value to MIWR */
    enc28j60_write_ctrl_reg16(ENC28J60_MIWRL, val);

    /* Wait until the write transaction is completed */
    while ((enc28j60_read_ctrl_reg(ENC28J60_MISTAT) & ENC28J60_MISTAT_BUSY) != 0) {
        portYIELD();
    }
}

static uint16_t enc28j60_read_phy_reg(uint8_t reg)
{
    uint8_t data[2];

    /* Write address of the PHY register to access to MIREGADR */
    enc28j60_write_ctrl_reg(ENC28J60_MIREGADR, reg);

    /* Set the MICMD.MIIRD bit to begin the read transaction */
    enc28j60_bit_field_set(ENC28J60_MICMD, ENC28J60_MICMD_MIIRD);

    /* Wait until the read transcation is completed */
    while ((enc28j60_read_ctrl_reg(ENC28J60_MISTAT) & ENC28J60_MISTAT_BUSY) != 0) {
        portYIELD();
    }

    /* Clear the MICMD.MIIRD bit */
    enc28j60_bit_field_clear(ENC28J60_MICMD, ENC28J60_MICMD_MIIRD);

    /* Read the desired data from MIRDL and MIRDH registers */
    data[0] = enc28j60_read_ctrl_reg(ENC28J60_MIRDL);
    data[1] = enc28j60_read_ctrl_reg(ENC28J60_MIRDH);

    return data[0] | (data[1] << 8);
}

static void enc28j60_write_buffer(const void *data, size_t size)
{
    enc28j60_select();
    enc28j60_write_byte(ENC28J60_WRITE_BUFFER_MEM);
    enc28j60_write(data, size);
    enc28j60_deselect();
}

static void enc28j60_read_buffer(void *data, size_t size)
{
    enc28j60_select();
    enc28j60_write_byte(ENC28J60_READ_BUFFER_MEM);
    enc28j60_read(data, size);
    enc28j60_deselect();
}

static void enc28j60_system_reset(void)
{
    enc28j60_select();
    enc28j60_write_byte(ENC28J60_SYSTEM_RESET);
    enc28j60_deselect();

    current_bank = ENC28J60_BANK_0_MASK;
    vTaskDelay(pdMS_TO_TICKS(10)); // Errata Rev. B7, Issue 1
}

static void enc28j60_gpio_init(void)
{
    /* Initialize SPI */
    gpio_set_function(ENC28J60_SPI_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(ENC28J60_SPI_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(ENC28J60_SPI_SCK_PIN, GPIO_FUNC_SPI);
    spi_init(ENC28J60_SPI_PORT, ENC28J60_SPI_BAUD_RATE);

    /* Initialize CS */
    gpio_init(ENC28J60_SPI_CS_PIN);
    gpio_set_dir(ENC28J60_SPI_CS_PIN, GPIO_OUT);
    gpio_put(ENC28J60_SPI_CS_PIN, true);
}

void enc28j60_init(const uint8_t *mac_addr)
{
    /* Initialize GPIOs */
    enc28j60_gpio_init();

    /* Create mutex to protect SPI bus access */
    spi_mutex = xSemaphoreCreateMutex();

    /* Perform software reset */
    enc28j60_system_reset();

    /* Configure Rx buffer range */
    enc28j60_write_ctrl_reg16(ENC28J60_ERXSTL, ENC28J60_RX_BUF_START);
    enc28j60_write_ctrl_reg16(ENC28J60_ERXNDL, ENC28J60_RX_BUF_END);
    enc28j60_write_ctrl_reg16(ENC28J60_ERXRDPTL, ENC28J60_RX_BUF_START);
    next_packet_ptr = ENC28J60_RX_BUF_START;

    /* Configure Tx buffer range */
    enc28j60_write_ctrl_reg16(ENC28J60_ETXSTL, ENC28J60_TX_BUF_START);
    enc28j60_write_ctrl_reg16(ENC28J60_ETXNDL, ENC28J60_TX_BUF_END);

    /* Packet filters: enable unicast, broadcast, reject packets with invalid CRC, enable pattern match */
    enc28j60_write_ctrl_reg(ENC28J60_ERXFCON, ENC28J60_ERXFCON_UCEN | ENC28J60_ERXFCON_CRCEN | ENC28J60_ERXFCON_PMEN | ENC28J60_ERXFCON_BCEN);

    /* Configure pattern match, allow only ARP packets for broadcast packets:
     * EtherType ARP: 06 08
     * Broadcast MAC: FF:FF:FF:FF:FF:FF
     * Checksum of these values is 0xF7F9
     *
     * The position mask of these bytes in Ethernet frame is:
     * 11 0000 0011 1111 -> 0x303F */
    enc28j60_write_ctrl_reg16(ENC28J60_EPMM0, 0x303F);
    enc28j60_write_ctrl_reg16(ENC28J60_EPMCSL, 0xF7F9);

    /* Enable MAC receive logic, allow sending and respond to PAUSE frames */
    enc28j60_write_ctrl_reg(ENC28J60_MACON1, ENC28J60_MACON1_RXPAUS | ENC28J60_MACON1_TXPAUS | ENC28J60_MACON1_MARXEN);

    /* Zero-pad short frames to 60 bits and append CRC, enable validation of frame length, half-duplex operation */
    enc28j60_write_ctrl_reg(ENC28J60_MACON3, ENC28J60_MACON3_PADCFG0 | ENC28J60_MACON3_TXCRCEN | ENC28J60_MACON3_FRMLNEN);

    /* Set maximum packet size which the controller will accept */
    enc28j60_write_ctrl_reg16(ENC28J60_MAMXFLL, ENC28J60_MAX_FRAME_LEN);

    /* Set back-to-back inter-frame gap as per datasheet for half-duplex mode */
    enc28j60_write_ctrl_reg(ENC28J60_MABBIPG, 0x12);

    /* Set non-back-to-back inter-frame gap as per datasheet for half-duplex mode */
    enc28j60_write_ctrl_reg(ENC28J60_MAIPGL, 0x12);
    enc28j60_write_ctrl_reg(ENC28J60_MAIPGH, 0x0C);

    /* Set MAC address */
    enc28j60_write_ctrl_reg(ENC28J60_MAADR1, mac_addr[0]);
    enc28j60_write_ctrl_reg(ENC28J60_MAADR2, mac_addr[1]);
    enc28j60_write_ctrl_reg(ENC28J60_MAADR3, mac_addr[2]);
    enc28j60_write_ctrl_reg(ENC28J60_MAADR4, mac_addr[3]);
    enc28j60_write_ctrl_reg(ENC28J60_MAADR5, mac_addr[4]);
    enc28j60_write_ctrl_reg(ENC28J60_MAADR6, mac_addr[5]);
    /* Disable half-duplex loopback */
    enc28j60_write_phy_reg(ENC28J60_PHCON2, ENC28J60_PHCON2_HDLDIS);

    /* Set LED config - stretch LED events by 70ms (normal), LEDA -> link status, LEDB -> Tx/Rx activity */
    enc28j60_write_phy_reg(ENC28J60_PHLCON, ENC28J60_PHLCON_LACFG2 |
                                            ENC28J60_PHLCON_LBCFG2 | ENC28J60_PHLCON_LBCFG1 | ENC28J60_PHLCON_LBCFG0);// |
                                            //ENC28J60_PHLCON_LFRQ0 | ENC28J60_PHLCON_STRCH);

    /* Start receiving */
    enc28j60_bit_field_set(ENC28J60_ECON1, ENC28J60_ECON1_RXEN);
}

void enc28j60_send_packet(const uint8_t *packet, size_t size)
{
    /* Errata Rev. B7, Issue 10 - reset transmit logic before attempting to send a packet */
    enc28j60_bit_field_set(ENC28J60_ECON1, ENC28J60_ECON1_TXRST);
    enc28j60_bit_field_clear(ENC28J60_ECON1, ENC28J60_ECON1_TXRST);
    enc28j60_bit_field_clear(ENC28J60_EIR, ENC28J60_EIR_TXIF | ENC28J60_EIR_TXERIF);

    /* Set write pointer to the start of transmit buffer area */
    enc28j60_write_ctrl_reg16(ENC28J60_EWRPTL, ENC28J60_TX_BUF_START);

    /* Set TXND pointer to correspond to the packet size */
    enc28j60_write_ctrl_reg16(ENC28J60_ETXNDL, ENC28J60_TX_BUF_START + size);

    /* Write per-packet control byte, cleared bit 0 means use MACON3 settings */
    const uint8_t ctrl = 0x00;
    enc28j60_write_buffer(&ctrl, sizeof(ctrl));

    /* Write packet to the buffer */
    enc28j60_write_buffer(packet, size);

    /* Trigger transmission */
    enc28j60_bit_field_set(ENC28J60_ECON1, ENC28J60_ECON1_TXRTS);

    /* Wait for transmission to complete TODO is it needed? */
    // while ((enc28j60_read_ctrl_reg(ENC28J60_EIR) & ENC28J60_EIR_TXIF) == 0) {}

    // TODO check errors
}

size_t enc28j60_receive_packet(uint8_t *packet, size_t max_size)
{
    uint16_t size;
    uint16_t status;

    // TODO check if we have any packet

    /* Set the read pointer to the start of the received packet */
    enc28j60_write_ctrl_reg16(ENC28J60_ERDPTL, next_packet_ptr);

    /* Read the next packet pointer */
    enc28j60_read_buffer(&next_packet_ptr, sizeof(next_packet_ptr));

    /* Read the packet length */
    enc28j60_read_buffer(&size, sizeof(size));
    size -= 4; // Remove the CRC count

    /* Prevent buffer overrun */
    if (size > max_size) {
        size = max_size;
    }

    /* Read the Rx status */
    enc28j60_read_buffer(&status, sizeof(status));

    // TODO check status & 0x80?

    /* Copy the packet from the receive buffer */
    enc28j60_read_buffer(packet, size);

    /* Moxe the Rx read pointer to the start of the next received packet */
    enc28j60_write_ctrl_reg16(ENC28J60_ERXRDPTL, next_packet_ptr); // TODO errata odd/even

    /* Decrement the packet counter */
    enc28j60_bit_field_set(ENC28J60_ECON2, ENC28J60_ECON2_PKTDEC);

    return size;
}

bool enc28j60_rx_packets_pending(void)
{
    return enc28j60_read_ctrl_reg(ENC28J60_EPKTCNT) > 0;
}

bool enc28j60_is_link_up(void)
{
    return (enc28j60_read_phy_reg(ENC28J60_PHSTAT2) & ENC28J60_PHSTAT2_LSTAT) != 0;
}

uint8_t enc28j60_get_revision(void)
{
    return enc28j60_read_ctrl_reg(ENC28J60_EREVID);
}
