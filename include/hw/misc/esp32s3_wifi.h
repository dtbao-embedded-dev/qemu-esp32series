/*
 * ESP32-S3 WiFi MAC emulation
 *
 * The register layout of the MAC page follows the ESP32-C3 model of
 * lcgamboa/qemu (branch picsimlab-esp32, commit 64aed503,
 * hw/misc/esp32c3_wifi.c); docs/wifi-s3-spike.md shows that the ESP32-S3
 * blobs use the same DMA registers at 0x60033000.
 *
 * The device is the "esp32s3_wifi" NIC model:
 *   -nic user,model=esp32s3_wifi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32S3_WIFI_H
#define ESP32S3_WIFI_H

#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "net/net.h"

#define TYPE_ESP32S3_WIFI "esp32s3_wifi"
#define ESP32S3_WIFI(obj) OBJECT_CHECK(ESP32S3WifiState, (obj), TYPE_ESP32S3_WIFI)

/* The blobs use 0x60032000-0x60035fff; the MAC page is the second one */
#define ESP32S3_WIFI_BASE       0x60032000
#define ESP32S3_WIFI_MEM_SIZE   0x4000
#define ESP32S3_WIFI_MAC        0x1000

/* MAC page registers, offsets from ESP32S3_WIFI_BASE */
REG32(ESP32S3_WIFI_STATUS, ESP32S3_WIFI_MAC + 0xcb0)
REG32(ESP32S3_WIFI_DMA_OUT_STATUS, ESP32S3_WIFI_MAC + 0xd14)
/* Free-running microsecond counter the PHY code uses for timeouts */
REG32(ESP32S3_WIFI_TIME_US, 0x3000)

typedef struct ESP32S3WifiState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    NICState *nic;
    NICConf conf;

    uint32_t mem[ESP32S3_WIFI_MEM_SIZE / 4];
} ESP32S3WifiState;

#endif /* ESP32S3_WIFI_H */
