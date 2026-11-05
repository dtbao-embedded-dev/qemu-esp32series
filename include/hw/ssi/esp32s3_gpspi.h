/*
 * ESP32-S3 general purpose SPI (GP-SPI2/GP-SPI3) master
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "hw/registerfields.h"
#include "hw/dma/esp_gdma.h"

#define TYPE_ESP32S3_GPSPI "esp32s3.gpspi"
#define ESP32S3_GPSPI(obj) \
    OBJECT_CHECK(ESP32S3GpSpiState, (obj), TYPE_ESP32S3_GPSPI)

#define ESP32S3_GPSPI_COUNT     2       /* SPI2 and SPI3 */
#define ESP32S3_GPSPI_CS_COUNT  6
#define ESP32S3_GPSPI_BUF_SIZE  64      /* W0..W15 */
#define ESP32S3_GPSPI_IO_SIZE   0x1000
#define ESP32S3_GPSPI_REG_COUNT (0x100 / 4)

REG32(SPI_CMD, 0x00)
    FIELD(SPI_CMD, CONF_BITLEN, 0, 18)
    FIELD(SPI_CMD, UPDATE, 23, 1)
    FIELD(SPI_CMD, USR, 24, 1)
REG32(SPI_ADDR, 0x04)
REG32(SPI_CTRL, 0x08)
    FIELD(SPI_CTRL, RD_BIT_ORDER, 23, 2)
    FIELD(SPI_CTRL, WR_BIT_ORDER, 25, 2)
REG32(SPI_CLOCK, 0x0C)
REG32(SPI_USER, 0x10)
    FIELD(SPI_USER, DOUTDIN, 0, 1)
    FIELD(SPI_USER, MISO_HIGHPART, 24, 1)
    FIELD(SPI_USER, MOSI_HIGHPART, 25, 1)
    FIELD(SPI_USER, USR_MOSI, 27, 1)
    FIELD(SPI_USER, USR_MISO, 28, 1)
    FIELD(SPI_USER, USR_DUMMY, 29, 1)
    FIELD(SPI_USER, USR_ADDR, 30, 1)
    FIELD(SPI_USER, USR_COMMAND, 31, 1)
REG32(SPI_USER1, 0x14)
    FIELD(SPI_USER1, USR_DUMMY_CYCLELEN, 0, 8)
    FIELD(SPI_USER1, USR_ADDR_BITLEN, 27, 5)
REG32(SPI_USER2, 0x18)
    FIELD(SPI_USER2, USR_COMMAND_VALUE, 0, 16)
    FIELD(SPI_USER2, USR_COMMAND_BITLEN, 28, 4)
REG32(SPI_MS_DLEN, 0x1C)
    FIELD(SPI_MS_DLEN, MS_DATA_BITLEN, 0, 18)
REG32(SPI_MISC, 0x20)
    FIELD(SPI_MISC, CS_DIS, 0, 6)
REG32(SPI_DIN_MODE, 0x24)
REG32(SPI_DIN_NUM, 0x28)
REG32(SPI_DOUT_MODE, 0x2C)
REG32(SPI_DMA_CONF, 0x30)
    FIELD(SPI_DMA_CONF, OUTFIFO_EMPTY, 0, 1)
    FIELD(SPI_DMA_CONF, INFIFO_FULL, 1, 1)
    FIELD(SPI_DMA_CONF, DMA_RX_ENA, 27, 1)
    FIELD(SPI_DMA_CONF, DMA_TX_ENA, 28, 1)
    FIELD(SPI_DMA_CONF, AFIFO_RST, 29, 3)   /* RX, BUF and DMA FIFO resets */
REG32(SPI_DMA_INT_ENA, 0x34)
REG32(SPI_DMA_INT_CLR, 0x38)
REG32(SPI_DMA_INT_RAW, 0x3C)
REG32(SPI_DMA_INT_ST, 0x40)
REG32(SPI_DMA_INT_SET, 0x44)
REG32(SPI_W0, 0x98)
REG32(SPI_SLAVE, 0xE0)
REG32(SPI_SLAVE1, 0xE4)
REG32(SPI_CLK_GATE, 0xE8)
REG32(SPI_DATE, 0xF0)

/* Bits of the SPI_DMA_INT_* registers (all 21 sources share them) */
#define ESP32S3_GPSPI_INT_MASK          0x1fffff
#define ESP32S3_GPSPI_INT_TRANS_DONE    BIT(12)

/* Reset values from the ESP-IDF register description (soc/spi_reg.h) */
#define ESP32S3_GPSPI_CTRL_RESET    0x003c0000  /* Q/D/HOLD/WP polarity */
#define ESP32S3_GPSPI_CLOCK_RESET   0x80003043
#define ESP32S3_GPSPI_USER_RESET    0x800000c0  /* command, CS setup/hold */
#define ESP32S3_GPSPI_USER1_RESET   0x00410000
#define ESP32S3_GPSPI_USER2_RESET   0x08000000
#define ESP32S3_GPSPI_MISC_RESET    0x0000003e  /* CS1..CS5 disabled */
#define ESP32S3_GPSPI_DMA_CONF_RESET 0x00000003 /* FIFO status bits */
#define ESP32S3_GPSPI_DATE_VALUE    0x2101190

typedef struct ESP32S3GpSpiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    SSIBus *bus;
    char *bus_name;
    /* GDMA controller (set by the machine) and this SPI's ID on it */
    ESPGdmaState *gdma;
    uint32_t gdma_periph;

    uint32_t regs[ESP32S3_GPSPI_REG_COUNT];
    uint8_t buf[ESP32S3_GPSPI_BUF_SIZE];
    uint32_t int_raw;
} ESP32S3GpSpiState;
