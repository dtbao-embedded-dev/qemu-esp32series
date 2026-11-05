/*
 * ESP32-S3 WiFi PHY/RF register stubs
 *
 * See include/hw/misc/esp32s3_wifi_phy.h. Each block keeps the last value
 * written to every register, and a few registers that the PHY library polls
 * read with the bits it waits for already set (or cleared).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/misc/esp32s3_wifi_phy.h"

/* Size of the TSENS window: a single register */
#define TSENS_SIZE          4
/* TSENS_READY (bit 8) set, raw reading 0x68, about 25 degrees C */
#define TSENS_VALUE         0x168

static uint32_t esp32s3_wifi_phy_ana_read(ESP32S3WifiPhyBlock *b, hwaddr addr)
{
    uint32_t stored = b->mem[addr / 4];

    switch (addr) {
    case 0x00:
    case 0x48:
        return 0x00ffffff;
    case 0x04:
        return 0xfdffffff;
    case 0x44:
    case 0x4c:
    case 0xc4:
        return 0xffffffff;
    case 0x50:
        /* rom_pkdet_vol_start waits for bits [26:24] == 7 */
        return stored | 0x07000000;
    case 0x168:
        /* wait_freq_set_busy waits for bit 31 (busy) to clear */
        return stored & ~0x80000000u;
    default:
        return stored;
    }
}

static uint64_t esp32s3_wifi_phy_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3WifiPhyBlock *b = opaque;

    switch (b->id) {
    case ESP32S3_WIFI_PHY_ANA:
        return esp32s3_wifi_phy_ana_read(b, addr);
    case ESP32S3_WIFI_PHY_FE:
        /* 0x174 bit 16: IQ estimation done (ram_iq_est_enable) */
        if (addr == 0x174 || addr == 0x7c) {
            return 0xffffffff;
        }
        return b->mem[addr / 4];
    case ESP32S3_WIFI_PHY_NRX:
    case ESP32S3_WIFI_PHY_BB:
        return 0xffffffff;
    case ESP32S3_WIFI_PHY_TSENS:
        return TSENS_VALUE;
    case ESP32S3_WIFI_PHY_FE2:
    default:
        return 0;
    }
}

static void esp32s3_wifi_phy_write(void *opaque, hwaddr addr, uint64_t value,
                                   unsigned int size)
{
    ESP32S3WifiPhyBlock *b = opaque;

    if (addr < sizeof(b->mem)) {
        b->mem[addr / 4] = value;
    }
}

static const MemoryRegionOps esp32s3_wifi_phy_ops = {
    .read = esp32s3_wifi_phy_read,
    .write = esp32s3_wifi_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32s3_wifi_phy_reset_hold(Object *obj, ResetType type)
{
    ESP32S3WifiPhyState *s = ESP32S3_WIFI_PHY(obj);

    for (int i = 0; i < ESP32S3_WIFI_PHY_BLOCK_COUNT; i++) {
        memset(s->block[i].mem, 0, sizeof(s->block[i].mem));
    }
}

static void esp32s3_wifi_phy_init(Object *obj)
{
    static const char *const names[ESP32S3_WIFI_PHY_BLOCK_COUNT] = {
        [ESP32S3_WIFI_PHY_ANA] = "esp32s3.wifi_phy.ana",
        [ESP32S3_WIFI_PHY_FE] = "esp32s3.wifi_phy.fe",
        [ESP32S3_WIFI_PHY_FE2] = "esp32s3.wifi_phy.fe2",
        [ESP32S3_WIFI_PHY_NRX] = "esp32s3.wifi_phy.nrx",
        [ESP32S3_WIFI_PHY_BB] = "esp32s3.wifi_phy.bb",
        [ESP32S3_WIFI_PHY_TSENS] = "esp32s3.wifi_phy.tsens",
    };
    ESP32S3WifiPhyState *s = ESP32S3_WIFI_PHY(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    for (int i = 0; i < ESP32S3_WIFI_PHY_BLOCK_COUNT; i++) {
        ESP32S3WifiPhyBlock *b = &s->block[i];

        b->phy = s;
        b->id = i;
        memory_region_init_io(&b->iomem, obj, &esp32s3_wifi_phy_ops, b, names[i],
                              i == ESP32S3_WIFI_PHY_TSENS ? TSENS_SIZE
                                                          : ESP32S3_WIFI_PHY_BLOCK_SIZE);
        sysbus_init_mmio(sbd, &b->iomem);
    }
}

static void esp32s3_wifi_phy_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32s3_wifi_phy_reset_hold;
}

static const TypeInfo esp32s3_wifi_phy_info = {
    .name = TYPE_ESP32S3_WIFI_PHY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3WifiPhyState),
    .instance_init = esp32s3_wifi_phy_init,
    .class_init = esp32s3_wifi_phy_class_init,
};

static void esp32s3_wifi_phy_register_types(void)
{
    type_register_static(&esp32s3_wifi_phy_info);
}

type_init(esp32s3_wifi_phy_register_types)
