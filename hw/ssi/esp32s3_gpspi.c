/*
 * ESP32-S3 general purpose SPI (GP-SPI2/GP-SPI3) master
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * Models the user-defined transaction of the GP-SPI master: setting
 * SPI_CMD.USR runs the command, address, dummy and data phases at once
 * on the SSI bus (named by the "bus-name" property), with data from and
 * to W0..W15, or through the GDMA OUT/IN channels bound to this SPI when
 * SPI_DMA_CONF.DMA_TX_ENA/DMA_RX_ENA is set. Every phase moves whole bytes
 * on a single data line, except
 * the dummy phase, which makes one transfer per dummy cycle as the QEMU SPI
 * flash model (m25p80) expects; the dual/quad/octal modes, clock timing and
 * the slave mode are not modelled.
 * The CS of the SSI device whose "cs" index matches an enabled CSn line
 * of SPI_MISC is held low for the transaction. The interrupt line follows
 * SPI_DMA_INT_RAW & SPI_DMA_INT_ENA.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/ssi/esp32s3_gpspi.h"

#define ESP32S3_GPSPI_W_END \
    (A_SPI_W0 + ESP32S3_GPSPI_BUF_SIZE - 4)

static inline uint32_t esp32s3_gpspi_reg(ESP32S3GpSpiState *s, hwaddr addr)
{
    return s->regs[addr / 4];
}

static void esp32s3_gpspi_update_irq(ESP32S3GpSpiState *s)
{
    uint32_t ena = esp32s3_gpspi_reg(s, A_SPI_DMA_INT_ENA);

    qemu_set_irq(s->irq, (s->int_raw & ena) != 0);
}

/* Drive the CS input of every SSI device on the bus: low when selected */
static void esp32s3_gpspi_set_cs(ESP32S3GpSpiState *s, bool active)
{
    uint32_t misc = esp32s3_gpspi_reg(s, A_SPI_MISC);
    uint32_t dis = FIELD_EX32(misc, SPI_MISC, CS_DIS);

    for (int i = 0; i < ESP32S3_GPSPI_CS_COUNT; i++) {
        DeviceState *dev = ssi_get_cs(s->bus, i);

        if (dev == NULL ||
            SSI_PERIPHERAL_GET_CLASS(dev)->cs_polarity == SSI_CS_NONE) {
            continue;
        }
        qemu_set_irq(qdev_get_gpio_in_named(dev, SSI_GPIO_CS, 0),
                     !(active && !(dis & BIT(i))));
    }
}

/* Shift one byte out and in, honouring the LSB-first bit order settings */
static uint8_t esp32s3_gpspi_xfer(ESP32S3GpSpiState *s, uint8_t out)
{
    uint32_t ctrl = esp32s3_gpspi_reg(s, A_SPI_CTRL);
    uint8_t in;

    if (FIELD_EX32(ctrl, SPI_CTRL, WR_BIT_ORDER) & 1) {
        out = revbit8(out);
    }
    in = ssi_transfer(s->bus, out);
    if (FIELD_EX32(ctrl, SPI_CTRL, RD_BIT_ORDER) & 1) {
        in = revbit8(in);
    }
    return in;
}

/* Number of bytes needed for a "bit length minus one" register field */
static inline uint32_t esp32s3_gpspi_bytes(uint32_t bitlen_m1)
{
    return DIV_ROUND_UP(bitlen_m1 + 1, 8);
}

/*
 * GDMA channel bound to this SPI in direction @dir (from the GDMA point of
 * view). The channel the driver started for this peripheral is preferred:
 * an unconfigured channel also reads PERI_SEL 0, the SPI2 ID.
 */
static bool esp32s3_gpspi_dma_chan(ESP32S3GpSpiState *s, int dir,
                                   uint32_t *chan)
{
    const uint32_t start = dir == ESP_GDMA_IN_IDX ? R_GDMA_IN_LINK_START_MASK
                                                  : R_GDMA_OUT_LINK_START_MASK;
    ESPGdmaClass *gc;

    if (s->gdma == NULL) {
        return false;
    }
    gc = ESP_GDMA_GET_CLASS(s->gdma);
    for (uint32_t i = 0; i < gc->m_channel_count; i++) {
        DmaConfigState *c = &s->gdma->ch_conf[dir][i];

        uint32_t periph = FIELD_EX32(c->peripheral, GDMA_PERI_SEL, PERI_SEL);

        if (periph == s->gdma_periph && (c->link & start)) {
            *chan = i;
            return true;
        }
    }
    return esp_gdma_get_channel_periph(s->gdma, s->gdma_periph, dir, chan);
}

/* Fetch @len bytes to send from the GDMA OUT channel */
static void esp32s3_gpspi_dma_tx(ESP32S3GpSpiState *s, uint8_t *tx,
                                 uint32_t len)
{
    uint32_t chan;

    if (!esp32s3_gpspi_dma_chan(s, ESP_GDMA_OUT_IDX, &chan) ||
        !esp_gdma_read_channel(s->gdma, chan, tx, len)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no valid GDMA OUT channel for "
                      "%u bytes\n", __func__, len);
    }
}

/* Store @len received bytes through the GDMA IN channel */
static void esp32s3_gpspi_dma_rx(ESP32S3GpSpiState *s, uint8_t *rx,
                                 uint32_t len)
{
    uint32_t chan;

    if (!esp32s3_gpspi_dma_chan(s, ESP_GDMA_IN_IDX, &chan) ||
        !esp_gdma_write_channel(s->gdma, chan, rx, len)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no valid GDMA IN channel for "
                      "%u bytes\n", __func__, len);
    }
}

static void esp32s3_gpspi_data_phase(ESP32S3GpSpiState *s, uint32_t user,
                                     uint32_t len)
{
    uint32_t conf = esp32s3_gpspi_reg(s, A_SPI_DMA_CONF);
    uint32_t tx_off = FIELD_EX32(user, SPI_USER, MOSI_HIGHPART) ? 32 : 0;
    uint32_t rx_off = FIELD_EX32(user, SPI_USER, MISO_HIGHPART) ? 32 : 0;
    bool mosi = FIELD_EX32(user, SPI_USER, USR_MOSI);
    bool miso = FIELD_EX32(user, SPI_USER, USR_MISO);
    bool full_duplex = FIELD_EX32(user, SPI_USER, DOUTDIN);
    bool dma_tx = mosi && FIELD_EX32(conf, SPI_DMA_CONF, DMA_TX_ENA);
    bool dma_rx = miso && FIELD_EX32(conf, SPI_DMA_CONF, DMA_RX_ENA);
    g_autofree uint8_t *tx = NULL;
    g_autofree uint8_t *rx = NULL;

    if (!dma_tx && !dma_rx && len > ESP32S3_GPSPI_BUF_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %u data bytes do not fit in "
                      "W0..W15 without DMA\n", __func__, len);
        len = ESP32S3_GPSPI_BUF_SIZE;
    }
    tx = g_malloc0(len);
    rx = g_malloc0(len);

    if (dma_tx) {
        esp32s3_gpspi_dma_tx(s, tx, len);
    } else {
        for (uint32_t i = 0; i < len; i++) {
            tx[i] = s->buf[(tx_off + i) % ESP32S3_GPSPI_BUF_SIZE];
        }
    }

    if (full_duplex) {
        /* MOSI and MISO share the clocks of a single data phase */
        for (uint32_t i = 0; i < len; i++) {
            rx[i] = esp32s3_gpspi_xfer(s, mosi ? tx[i] : 0);
        }
    } else {
        /* Half duplex: a write phase, then a read phase, both of len bytes */
        if (mosi) {
            for (uint32_t i = 0; i < len; i++) {
                esp32s3_gpspi_xfer(s, tx[i]);
            }
        }
        if (miso) {
            for (uint32_t i = 0; i < len; i++) {
                rx[i] = esp32s3_gpspi_xfer(s, 0);
            }
        }
    }

    if (dma_rx) {
        esp32s3_gpspi_dma_rx(s, rx, len);
    } else if (miso) {
        for (uint32_t i = 0; i < len; i++) {
            s->buf[(rx_off + i) % ESP32S3_GPSPI_BUF_SIZE] = rx[i];
        }
    }
}

static void esp32s3_gpspi_transaction(ESP32S3GpSpiState *s)
{
    uint32_t user = esp32s3_gpspi_reg(s, A_SPI_USER);
    uint32_t user1 = esp32s3_gpspi_reg(s, A_SPI_USER1);
    uint32_t user2 = esp32s3_gpspi_reg(s, A_SPI_USER2);

    esp32s3_gpspi_set_cs(s, true);

    if (FIELD_EX32(user, SPI_USER, USR_COMMAND)) {
        /* The command goes out low byte first (the driver pre-swaps it) */
        uint32_t value = FIELD_EX32(user2, SPI_USER2, USR_COMMAND_VALUE);
        uint32_t n = esp32s3_gpspi_bytes(FIELD_EX32(user2, SPI_USER2,
                                                    USR_COMMAND_BITLEN));
        for (uint32_t i = 0; i < n; i++) {
            esp32s3_gpspi_xfer(s, value >> (8 * i));
        }
    }

    if (FIELD_EX32(user, SPI_USER, USR_ADDR)) {
        /* MSB-first: shifted out from bit 31; LSB-first: from bit 0 */
        uint32_t addr = esp32s3_gpspi_reg(s, A_SPI_ADDR);
        uint32_t n = esp32s3_gpspi_bytes(FIELD_EX32(user1, SPI_USER1,
                                                    USR_ADDR_BITLEN));
        bool lsb = FIELD_EX32(esp32s3_gpspi_reg(s, A_SPI_CTRL), SPI_CTRL,
                              WR_BIT_ORDER) & 1;
        for (uint32_t i = 0; i < n; i++) {
            esp32s3_gpspi_xfer(s, lsb ? addr >> (8 * i) : addr >> (24 - 8 * i));
        }
    }

    if (FIELD_EX32(user, SPI_USER, USR_DUMMY)) {
        /* QEMU SSI flashes (m25p80) take one transfer per dummy cycle */
        uint32_t n = FIELD_EX32(user1, SPI_USER1, USR_DUMMY_CYCLELEN) + 1;
        for (uint32_t i = 0; i < n; i++) {
            esp32s3_gpspi_xfer(s, 0xff);
        }
    }

    if (FIELD_EX32(user, SPI_USER, USR_MOSI) ||
        FIELD_EX32(user, SPI_USER, USR_MISO)) {
        uint32_t bitlen = FIELD_EX32(esp32s3_gpspi_reg(s, A_SPI_MS_DLEN),
                                     SPI_MS_DLEN, MS_DATA_BITLEN);
        esp32s3_gpspi_data_phase(s, user, esp32s3_gpspi_bytes(bitlen));
    }

    esp32s3_gpspi_set_cs(s, false);

    s->int_raw |= ESP32S3_GPSPI_INT_TRANS_DONE;
    esp32s3_gpspi_update_irq(s);
}

static uint64_t esp32s3_gpspi_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3GpSpiState *s = ESP32S3_GPSPI(opaque);

    switch (addr) {
    case A_SPI_CMD:
        /* USR and UPDATE complete at once, only CONF_BITLEN reads back */
        return esp32s3_gpspi_reg(s, A_SPI_CMD) & R_SPI_CMD_CONF_BITLEN_MASK;
    case A_SPI_DMA_INT_CLR:
    case A_SPI_DMA_INT_SET:
        return 0;
    case A_SPI_DMA_INT_RAW:
        return s->int_raw;
    case A_SPI_DMA_INT_ST:
        return s->int_raw & esp32s3_gpspi_reg(s, A_SPI_DMA_INT_ENA);
    case A_SPI_W0 ... ESP32S3_GPSPI_W_END:
        return ldl_le_p(&s->buf[addr - A_SPI_W0]);
    case A_SPI_DATE:
        return ESP32S3_GPSPI_DATE_VALUE;
    default:
        if (addr < ESP32S3_GPSPI_REG_COUNT * 4) {
            return esp32s3_gpspi_reg(s, addr);
        }
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read at 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}

static void esp32s3_gpspi_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    ESP32S3GpSpiState *s = ESP32S3_GPSPI(opaque);
    uint32_t v = value;

    switch (addr) {
    case A_SPI_CMD:
        s->regs[addr / 4] = v & R_SPI_CMD_CONF_BITLEN_MASK;
        if (v & R_SPI_CMD_USR_MASK) {
            esp32s3_gpspi_transaction(s);
        }
        break;
    case A_SPI_DMA_CONF:
        /* FIFO resets self-clear, the FIFO status bits are read-only */
        v &= ~(R_SPI_DMA_CONF_AFIFO_RST_MASK |
               R_SPI_DMA_CONF_OUTFIFO_EMPTY_MASK |
               R_SPI_DMA_CONF_INFIFO_FULL_MASK);
        s->regs[addr / 4] = v | ESP32S3_GPSPI_DMA_CONF_RESET;
        break;
    case A_SPI_DMA_INT_ENA:
        s->regs[addr / 4] = v & ESP32S3_GPSPI_INT_MASK;
        esp32s3_gpspi_update_irq(s);
        break;
    case A_SPI_DMA_INT_CLR:
        s->int_raw &= ~v;
        esp32s3_gpspi_update_irq(s);
        break;
    case A_SPI_DMA_INT_SET:
        s->int_raw |= v & ESP32S3_GPSPI_INT_MASK;
        esp32s3_gpspi_update_irq(s);
        break;
    case A_SPI_DMA_INT_RAW:
    case A_SPI_DMA_INT_ST:
    case A_SPI_DATE:
        break;
    case A_SPI_W0 ... ESP32S3_GPSPI_W_END:
        stl_le_p(&s->buf[addr - A_SPI_W0], v);
        break;
    default:
        if (addr < ESP32S3_GPSPI_REG_COUNT * 4) {
            s->regs[addr / 4] = v;
            break;
        }
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at 0x%"
                      HWADDR_PRIx " = 0x%" PRIx32 "\n", __func__, addr, v);
        break;
    }
}

static const MemoryRegionOps esp32s3_gpspi_ops = {
    .read = esp32s3_gpspi_read,
    .write = esp32s3_gpspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void esp32s3_gpspi_reset_hold(Object *obj, ResetType type)
{
    ESP32S3GpSpiState *s = ESP32S3_GPSPI(obj);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->buf, 0, sizeof(s->buf));
    s->regs[A_SPI_CTRL / 4] = ESP32S3_GPSPI_CTRL_RESET;
    s->regs[A_SPI_CLOCK / 4] = ESP32S3_GPSPI_CLOCK_RESET;
    s->regs[A_SPI_USER / 4] = ESP32S3_GPSPI_USER_RESET;
    s->regs[A_SPI_USER1 / 4] = ESP32S3_GPSPI_USER1_RESET;
    s->regs[A_SPI_USER2 / 4] = ESP32S3_GPSPI_USER2_RESET;
    s->regs[A_SPI_MISC / 4] = ESP32S3_GPSPI_MISC_RESET;
    s->regs[A_SPI_DMA_CONF / 4] = ESP32S3_GPSPI_DMA_CONF_RESET;
    s->int_raw = 0;
    esp32s3_gpspi_update_irq(s);
    if (s->bus) {
        esp32s3_gpspi_set_cs(s, false);
    }
}

static void esp32s3_gpspi_realize(DeviceState *dev, Error **errp)
{
    ESP32S3GpSpiState *s = ESP32S3_GPSPI(dev);

    s->bus = ssi_create_bus(dev, s->bus_name ? s->bus_name : "spi");
}

static void esp32s3_gpspi_init(Object *obj)
{
    ESP32S3GpSpiState *s = ESP32S3_GPSPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32s3_gpspi_ops, s,
                          TYPE_ESP32S3_GPSPI, ESP32S3_GPSPI_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property esp32s3_gpspi_properties[] = {
    DEFINE_PROP_STRING("bus-name", ESP32S3GpSpiState, bus_name),
    DEFINE_PROP_UINT32("gdma-periph", ESP32S3GpSpiState, gdma_periph,
                       GDMA_SPI2),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_gpspi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32s3_gpspi_reset_hold;
    dc->realize = esp32s3_gpspi_realize;
    device_class_set_props(dc, esp32s3_gpspi_properties);
}

static const TypeInfo esp32s3_gpspi_info = {
    .name = TYPE_ESP32S3_GPSPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3GpSpiState),
    .instance_init = esp32s3_gpspi_init,
    .class_init = esp32s3_gpspi_class_init,
};

static void esp32s3_gpspi_register_types(void)
{
    type_register_static(&esp32s3_gpspi_info);
}

type_init(esp32s3_gpspi_register_types)
