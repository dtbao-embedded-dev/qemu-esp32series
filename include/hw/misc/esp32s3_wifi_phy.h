/*
 * ESP32-S3 WiFi PHY/RF register stubs
 *
 * The closed PHY library calibrates the radio through these blocks and
 * polls status bits that only real RF hardware would set. The stubs return
 * values that let it finish; nothing is measured or modelled. The values
 * follow lcgamboa/qemu (branch picsimlab-esp32, commit 64aed503,
 * hw/misc/esp32_fe.c, hw/misc/esp32c3_ana.c and the BB/NRX/FE2 stubs of
 * hw/riscv/esp32c3_picsimlab.c), plus the ESP32-S3 wait loops listed in
 * docs/wifi-s3-spike.md.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32S3_WIFI_PHY_H
#define ESP32S3_WIFI_PHY_H

#include "hw/sysbus.h"

#define TYPE_ESP32S3_WIFI_PHY "esp32s3.wifi_phy"
#define ESP32S3_WIFI_PHY(obj) \
    OBJECT_CHECK(ESP32S3WifiPhyState, (obj), TYPE_ESP32S3_WIFI_PHY)

#define ESP32S3_WIFI_PHY_BLOCK_SIZE 0x1000
/* Analog I2C master (I2C_MST_ANA_CONF0_REG is at +0x40) */
#define ESP32S3_WIFI_PHY_ANA_BASE   0x6000e000
/* SENS_SAR_TSENS_CTRL_REG */
#define ESP32S3_WIFI_PHY_TSENS_OFFSET 0x50

/* MMIO regions of the device, in sysbus_mmio index order */
typedef enum {
    ESP32S3_WIFI_PHY_ANA,       /* analog I2C master, 0x6000e000 */
    ESP32S3_WIFI_PHY_FE,        /* 0x60006000 */
    ESP32S3_WIFI_PHY_FE2,       /* 0x60005000 */
    ESP32S3_WIFI_PHY_NRX,       /* page of DR_REG_NRX_BASE, 0x6001c000 */
    ESP32S3_WIFI_PHY_BB,        /* 0x6001d000 */
    ESP32S3_WIFI_PHY_TSENS,     /* SENS_SAR_TSENS_CTRL_REG, 0x60008850 */
    ESP32S3_WIFI_PHY_BLOCK_COUNT
} ESP32S3WifiPhyBlockId;

typedef struct ESP32S3WifiPhyState ESP32S3WifiPhyState;

typedef struct ESP32S3WifiPhyBlock {
    MemoryRegion iomem;
    ESP32S3WifiPhyState *phy;
    ESP32S3WifiPhyBlockId id;
    uint32_t mem[ESP32S3_WIFI_PHY_BLOCK_SIZE / 4];
} ESP32S3WifiPhyBlock;

struct ESP32S3WifiPhyState {
    SysBusDevice parent_obj;

    ESP32S3WifiPhyBlock block[ESP32S3_WIFI_PHY_BLOCK_COUNT];
};

#endif /* ESP32S3_WIFI_PHY_H */
