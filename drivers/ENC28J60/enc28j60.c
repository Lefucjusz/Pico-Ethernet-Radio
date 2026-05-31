#include "enc28j60.h"
#include "enc28j60_defs.h"
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <pico/time.h>
#include <FreeRTOS.h>
#include <task.h>

// TODO add IRQ?

#define ENC28J60_SPI_PORT       spi1
#define ENC28J60_SPI_BAUD_RATE  (1 * 1000 * 1000) // TODO speed up

#define ENC28J60_SPI_MOSI_PIN   11
#define ENC28J60_SPI_MISO_PIN   12
#define ENC28J60_SPI_SCK_PIN    10
#define ENC28J60_SPI_CS_PIN     13

/* Rx buf at 0x0000 is needed for errata Rev. B7, 3 */
#define ENC28J60_TX_BUF_SIZE 0x600
#define ENC28J60_RX_BUF_START ENC28J60_RAM_START
#define ENC28J60_RX_BUF_END (ENC28J60_RAM_END - ENC28J60_TX_BUF_SIZE - 1)
#define ENC28J60_TX_BUF_START (ENC28J60_RX_BUF_END + 1)
#define ENC28J60_TX_BUF_END ENC28J60_RAM_END

#define ENC28J60_MAX_FRAME_LEN 1500

static const uint8_t mac_addr[] = {0xDE, 0xAD, 0xBA, 0xBE, 0xCA, 0xFE};

static uint8_t current_bank;

inline static void enc28j60_select(void)
{
    gpio_put(ENC28J60_SPI_CS_PIN, false);
}

inline static void enc28j60_deselect(void)
{
    gpio_put(ENC28J60_SPI_CS_PIN, true);
}

// inline static void enc28j60_write(const uint8_t *data, size_t size)
// {
//     spi_write_blocking(ENC28J60_SPI_PORT, data, size);
// }

inline static void enc28j60_write_byte(uint8_t val)
{
    spi_write_blocking(ENC28J60_SPI_PORT, &val, sizeof(val));
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
    /* PHY registers are accessed indirectly. First write address of the PHY register
     * to access to MIREGADR, then write the value to MIWRx and wait until write
     * transaction is completed. */
    enc28j60_write_ctrl_reg(ENC28J60_MIREGADR, reg);
    enc28j60_write_ctrl_reg16(ENC28J60_MIWRL, val);
    while ((enc28j60_read_ctrl_reg(ENC28J60_MISTAT) & ENC28J60_MISTAT_BUSY) != 0) {}
}

static uint16_t enc28j60_read_phy_reg(uint8_t reg, uint16_t val)
{
    // TODO
}

static void enc28j60_system_reset(void)
{
    enc28j60_select();
    enc28j60_write_byte(ENC28J60_SYSTEM_RESET);
    enc28j60_deselect();

    current_bank = ENC28J60_BANK_0_MASK;
    vTaskDelay(pdMS_TO_TICKS(10)); // Errata Rev. B7, 1
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
    enc28j60_deselect();
}

void enc28j60_init(void)
{
    /* Initialize GPIOs */
    enc28j60_gpio_init();

    /* Perform software reset */
    enc28j60_system_reset();

    /* Configure Rx buffer range */
    enc28j60_write_ctrl_reg16(ENC28J60_ERXSTL, ENC28J60_RX_BUF_START);
    enc28j60_write_ctrl_reg16(ENC28J60_ERXNDL, ENC28J60_RX_BUF_END);
    enc28j60_write_ctrl_reg16(ENC28J60_ERXRDPTL, ENC28J60_RX_BUF_START);

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
    enc28j60_write_ctrl_reg(ENC28J60_MACON1, ENC28J60_MACON1_TXPAUS | ENC28J60_MACON1_TXPAUS | ENC28J60_MACON1_MARXEN);

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
    enc28j60_write_ctrl_reg(ENC28J60_MAADR6, mac_addr[5]); // TODO verify order

    /* Disable half-duplex loopback */
    enc28j60_write_phy_reg(ENC28J60_PHCON2, ENC28J60_PHCON2_HDLDIS);

    /* Set LED config - stretch LED events by 70ms (normal), LEDA -> link status, LEDB -> Tx/Rx activity */
    enc28j60_write_phy_reg(ENC28J60_PHLCON, ENC28J60_PHLCON_LACFG2 |
                                            ENC28J60_PHLCON_LBCFG2 | ENC28J60_PHLCON_LBCFG1 | ENC28J60_PHLCON_LBCFG0 |
                                            ENC28J60_PHLCON_LFRQ0 | ENC28J60_PHLCON_STRCH);

    /* Start receiving */
    enc28j60_bit_field_set(ENC28J60_ECON1, ENC28J60_ECON1_RXEN);
}


uint8_t enc28j60_get_revision(void)
{
    return enc28j60_read_ctrl_reg(ENC28J60_EREVID);
}
