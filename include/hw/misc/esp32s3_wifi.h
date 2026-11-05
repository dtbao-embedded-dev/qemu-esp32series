/*
 * ESP32-S3 WiFi MAC emulation
 *
 * The register layout of the MAC page follows the ESP32-C3 model of
 * lcgamboa/qemu (branch picsimlab-esp32, commit 64aed503,
 * hw/misc/esp32c3_wifi.c); docs/wifi-s3-spike.md shows that the ESP32-S3
 * blobs use the same DMA registers at 0x60033000. Frames go to and come
 * from the emulated access point of hw/misc/esp32_wifi_ap.c.
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

#include "hw/misc/esp32_wifi.h"

#define TYPE_ESP32S3_WIFI "esp32s3_wifi"
#define ESP32S3_WIFI(obj) OBJECT_CHECK(ESP32S3WifiState, (obj), TYPE_ESP32S3_WIFI)

/* The blobs use 0x60032000-0x60035fff */
#define ESP32S3_WIFI_BASE       0x60032000
#define ESP32S3_WIFI_MEM_SIZE   0x4000
/* MAC page (DR_REG_WIFI_BASE on the C3) and PHYA page */
#define ESP32S3_WIFI_MAC        0x1000
#define ESP32S3_WIFI_PHYA       0x2000

/* Registers, as offsets from ESP32S3_WIFI_BASE */
REG32(ESP32S3_WIFI_MAC_ADDR0_LO, ESP32S3_WIFI_MAC + 0x040)
REG32(ESP32S3_WIFI_MAC_ADDR0_HI, ESP32S3_WIFI_MAC + 0x044)
REG32(ESP32S3_WIFI_MAC_ADDR1_LO, ESP32S3_WIFI_MAC + 0x048)
REG32(ESP32S3_WIFI_DMA_IN_STATUS, ESP32S3_WIFI_MAC + 0x084)
REG32(ESP32S3_WIFI_DMA_INLINK, ESP32S3_WIFI_MAC + 0x088)
REG32(ESP32S3_WIFI_NEXT_RX_DSCR, ESP32S3_WIFI_MAC + 0x08c)
REG32(ESP32S3_WIFI_LAST_RX_DSCR, ESP32S3_WIFI_MAC + 0x090)
REG32(ESP32S3_WIFI_DMA_INT_STATUS, ESP32S3_WIFI_MAC + 0xc3c)
REG32(ESP32S3_WIFI_DMA_INT_CLR, ESP32S3_WIFI_MAC + 0xc40)
REG32(ESP32S3_WIFI_DMA_ADDR_OFFSET, ESP32S3_WIFI_MAC + 0xc64)
REG32(ESP32S3_WIFI_STATUS, ESP32S3_WIFI_MAC + 0xcb0)
REG32(ESP32S3_WIFI_DMA_OUTLINK, ESP32S3_WIFI_MAC + 0xd08)
REG32(ESP32S3_WIFI_DMA_OUT_STATUS, ESP32S3_WIFI_MAC + 0xd14)
/* TX acknowledge status of the last frame */
REG32(ESP32S3_WIFI_PHYA_PKT_STATUS, ESP32S3_WIFI_PHYA + 0x320)
/* Free-running microsecond counter the PHY code uses for timeouts */
REG32(ESP32S3_WIFI_TIME_US, 0x3000)

/* DMA_OUTLINK: start bits and the low 20 bits of the descriptor address */
#define ESP32S3_WIFI_OUTLINK_START  0xc0000000u
#define ESP32S3_WIFI_OUTLINK_ADDR   0x000fffffu
/* Interrupt bits, as on the C3 */
#define ESP32S3_WIFI_INT_RX         0x01004000u
#define ESP32S3_WIFI_INT_TX_DONE    0x00000080u

typedef struct ESP32S3WifiState {
    /* Must stay first: the access point code uses it as the device */
    Esp32WifiState parent_obj;

    uint32_t regs[ESP32S3_WIFI_MEM_SIZE / 4];
} ESP32S3WifiState;

#endif /* ESP32S3_WIFI_H */
