/*
 * Per-packet TX status for the emulated WiFi access point
 *
 * hw/misc/esp32_wifi_ap.c (from lcgamboa/qemu, branch picsimlab-esp32,
 * commit 64aed503) reports whether a transmitted frame was acknowledged
 * through Esp32_WLAN_Set_Packet_Status(). In that tree a PHYA device stores
 * it; on the esp32s3 machine the WiFi MAC model (hw/misc/esp32s3_wifi.c)
 * implements it.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32_PHYA_H
#define ESP32_PHYA_H

#define ESP32_PHYA_NACK 0x00002300
#define ESP32_PHYA_ACK  0x0

void Esp32_WLAN_Set_Packet_Status(const uint32_t state);

#endif /* ESP32_PHYA_H */
