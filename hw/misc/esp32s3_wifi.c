/*
 * ESP32-S3 WiFi MAC emulation
 *
 * See include/hw/misc/esp32s3_wifi.h. The DMA handling follows
 * hw/misc/esp32c3_wifi.c of lcgamboa/qemu (branch picsimlab-esp32, commit
 * 64aed503): a write to DMA_OUTLINK sends one frame to the access point
 * and raises TX_DONE; a frame from the access point is written into the RX
 * descriptor list given in DMA_INLINK and raises the RX interrupt. Other
 * registers keep the last written value.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "exec/address-spaces.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/misc/esp32s3_wifi.h"
#include "hw/misc/esp32_phya.h"
#include "esp32_wlan.h"
#include "esp32_wlan_packet.h"

/* The access point code calls back into the MAC without a device pointer */
static ESP32S3WifiState *esp32s3_wifi_dev;

static void esp32s3_wifi_set_interrupt(Esp32WifiState *s, uint32_t bits)
{
    s->raw_interrupt |= bits;
    qemu_irq_raise(s->irq);
}

void Esp32_WLAN_frame_delivered(Esp32WifiState *s)
{
    esp32s3_wifi_set_interrupt(s, ESP32S3_WIFI_INT_TX_DONE);
}

void Esp32_WLAN_Set_Packet_Status(const uint32_t state)
{
    if (esp32s3_wifi_dev) {
        esp32s3_wifi_dev->regs[R_ESP32S3_WIFI_PHYA_PKT_STATUS] = state;
    }
}

/* The station MAC address the firmware programmed into the MAC */
static void esp32s3_wifi_update_macaddr(ESP32S3WifiState *d)
{
    uint32_t lo = d->regs[R_ESP32S3_WIFI_MAC_ADDR0_LO];
    uint32_t hi = d->regs[R_ESP32S3_WIFI_MAC_ADDR0_HI];
    uint8_t *mac = d->parent_obj.macaddr;

    mac[0] = lo;
    mac[1] = lo >> 8;
    mac[2] = lo >> 16;
    mac[3] = lo >> 24;
    mac[4] = hi;
    mac[5] = hi >> 8;
}

static bool esp32s3_wifi_match_mac(const uint8_t *addr, const uint32_t *reg)
{
    return !memcmp(addr, reg, 6) || !memcmp(addr, BROADCAST, 6);
}

/* Frame from the access point to the firmware */
void Esp32_sendFrame(Esp32WifiState *s, mac80211_frame *frame, int length,
                     int signal_strength)
{
    ESP32S3WifiState *d = (ESP32S3WifiState *)s;
    const uint32_t *mac0 = &d->regs[R_ESP32S3_WIFI_MAC_ADDR0_LO];
    const uint32_t *mac1 = &d->regs[R_ESP32S3_WIFI_MAC_ADDR1_LO];
    size_t total = sizeof(wifi_pkt_rx_ctrl_c3_t) + length;
    g_autofree uint8_t *buf = NULL;
    wifi_pkt_rx_ctrl_c3_t *pkt;
    dma_list_item item;

    if (s->dma_inlink_address == 0 || length <= 0) {
        return;
    }
    buf = g_malloc0(total);
    pkt = (wifi_pkt_rx_ctrl_c3_t *)buf;
    *pkt = (wifi_pkt_rx_ctrl_c3_t) {
        .rssi = signal_strength + g_random_int_range(0, 10),
        .rate = 11,
        .sig_len = length,
        .sig_len_copy = length,
        .legacy_length = length,
        .noise_floor = -97,
        .channel = esp32_wifi_channel,
        .timestamp = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000,
    };
    /*
     * The MAC flags frames whose destination or BSSID matches one of the two
     * addresses the firmware programmed.
     */
    pkt->damatch0 = esp32s3_wifi_match_mac(frame->destination_address, mac0);
    pkt->damatch1 = esp32s3_wifi_match_mac(frame->destination_address, mac1);
    pkt->bssidmatch0 = esp32s3_wifi_match_mac(frame->bssid_address, mac0);
    pkt->bssidmatch1 = esp32s3_wifi_match_mac(frame->bssid_address, mac1);
    memcpy(buf + sizeof(*pkt), frame, length);

    /*
     * Write only where the hardware would: a descriptor the driver has
     * handed over (owner set) with room for the frame. Anything else would
     * corrupt the guest heap, so such a frame is dropped.
     */
    address_space_read(&address_space_memory, s->dma_inlink_ptr,
                       MEMTXATTRS_UNSPECIFIED, &item, sizeof(item));
    if (!item.owner || item.address == 0 || item.size < total) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp32s3_wifi: RX frame dropped, descriptor 0x%08x "
                      "owner=%u size=%u, frame %zu bytes\n",
                      s->dma_inlink_ptr, item.owner, item.size, total);
        return;
    }
    address_space_write(&address_space_memory, item.address,
                        MEMTXATTRS_UNSPECIFIED, buf, total);
    item.length = total;
    item.eof = 1;
    /* Only the first word (size, length, flags) changes */
    address_space_write(&address_space_memory, s->dma_inlink_ptr,
                        MEMTXATTRS_UNSPECIFIED, &item, 4);
    s->dma_inlink_ptr = item.next ? item.next : s->dma_inlink_address;
    d->regs[R_ESP32S3_WIFI_NEXT_RX_DSCR] = s->dma_inlink_ptr;
    esp32s3_wifi_set_interrupt(s, ESP32S3_WIFI_INT_RX);
}

/* Frame from the firmware to the access point */
static void esp32s3_wifi_transmit(ESP32S3WifiState *d, uint32_t outlink)
{
    Esp32WifiState *s = &d->parent_obj;
    uint32_t base = d->regs[R_ESP32S3_WIFI_DMA_ADDR_OFFSET];
    g_autofree mac80211_frame *frame = g_new0(mac80211_frame, 1);
    const size_t max_len = offsetof(mac80211_frame, frame_length);
    dma_list_item item;
    size_t len;

    if (base == 0) {
        base = 0x3fc00000;
    }
    address_space_read(&address_space_memory,
                       base | (outlink & ESP32S3_WIFI_OUTLINK_ADDR),
                       MEMTXATTRS_UNSPECIFIED, &item, sizeof(item));
    len = item.length;
    if (len > max_len) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp32s3_wifi: TX frame of %zu bytes truncated to %zu\n",
                      len, max_len);
        len = max_len;
    }
    address_space_read(&address_space_memory, item.address,
                       MEMTXATTRS_UNSPECIFIED, frame, len);
    frame->frame_length = len;
    frame->next_frame = NULL;
    Esp32_WLAN_handle_frame(s, frame);
    esp32s3_wifi_set_interrupt(s, ESP32S3_WIFI_INT_TX_DONE);
}

static uint64_t esp32s3_wifi_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3WifiState *d = ESP32S3_WIFI(opaque);
    Esp32WifiState *s = &d->parent_obj;

    switch (addr) {
    case A_ESP32S3_WIFI_DMA_INLINK:
        return s->dma_inlink_address;
    case A_ESP32S3_WIFI_DMA_IN_STATUS:
        return d->regs[addr / 4] & ~1u;
    case A_ESP32S3_WIFI_DMA_INT_STATUS:
    case A_ESP32S3_WIFI_DMA_INT_CLR:
        return s->raw_interrupt;
    case A_ESP32S3_WIFI_STATUS:
    case A_ESP32S3_WIFI_DMA_OUT_STATUS:
        return 1;
    case A_ESP32S3_WIFI_TIME_US:
        return (uint32_t)qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    default:
        return d->regs[addr / 4];
    }
}

static void esp32s3_wifi_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32S3WifiState *d = ESP32S3_WIFI(opaque);
    Esp32WifiState *s = &d->parent_obj;

    d->regs[addr / 4] = value;

    switch (addr) {
    case A_ESP32S3_WIFI_MAC_ADDR0_LO:
    case A_ESP32S3_WIFI_MAC_ADDR0_HI:
        esp32s3_wifi_update_macaddr(d);
        break;
    case A_ESP32S3_WIFI_DMA_INLINK:
        s->dma_inlink_address = value;
        s->dma_inlink_ptr = value;
        d->regs[R_ESP32S3_WIFI_NEXT_RX_DSCR] = value;
        d->regs[R_ESP32S3_WIFI_LAST_RX_DSCR] = value;
        break;
    case A_ESP32S3_WIFI_DMA_INT_CLR:
        s->raw_interrupt &= ~value;
        if (s->raw_interrupt == 0) {
            qemu_irq_lower(s->irq);
        }
        break;
    case A_ESP32S3_WIFI_DMA_OUTLINK:
        if (value & ESP32S3_WIFI_OUTLINK_START) {
            esp32s3_wifi_transmit(d, value);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps esp32s3_wifi_ops = {
    .read = esp32s3_wifi_read,
    .write = esp32s3_wifi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32s3_wifi_reset_hold(Object *obj, ResetType type)
{
    ESP32S3WifiState *d = ESP32S3_WIFI(obj);
    Esp32WifiState *s = &d->parent_obj;

    memset(d->regs, 0, sizeof(d->regs));
    memset(s->macaddr, 0, sizeof(s->macaddr));
    s->dma_inlink_address = 0;
    s->dma_inlink_ptr = 0;
    s->raw_interrupt = 0;
    qemu_irq_lower(s->irq);
    Esp32_WLAN_reset_ap(s);
}

static void esp32s3_wifi_realize(DeviceState *dev, Error **errp)
{
    ESP32S3WifiState *d = ESP32S3_WIFI(dev);

    qemu_macaddr_default_if_unset(&d->parent_obj.conf.macaddr);
    Esp32_WLAN_setup_ap(dev, &d->parent_obj);
    esp32s3_wifi_dev = d;
}

static void esp32s3_wifi_init(Object *obj)
{
    ESP32S3WifiState *d = ESP32S3_WIFI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&d->parent_obj.iomem, obj, &esp32s3_wifi_ops, d,
                          TYPE_ESP32S3_WIFI, ESP32S3_WIFI_MEM_SIZE);
    sysbus_init_mmio(sbd, &d->parent_obj.iomem);
    sysbus_init_irq(sbd, &d->parent_obj.irq);
}

static Property esp32s3_wifi_properties[] = {
    DEFINE_NIC_PROPERTIES(ESP32S3WifiState, parent_obj.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_wifi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp32s3_wifi_realize;
    dc->desc = "ESP32-S3 WiFi";
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    device_class_set_props(dc, esp32s3_wifi_properties);
    rc->phases.hold = esp32s3_wifi_reset_hold;
}

static const TypeInfo esp32s3_wifi_info = {
    .name = TYPE_ESP32S3_WIFI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3WifiState),
    .instance_init = esp32s3_wifi_init,
    .class_init = esp32s3_wifi_class_init,
};

static void esp32s3_wifi_register_types(void)
{
    type_register_static(&esp32s3_wifi_info);
}

type_init(esp32s3_wifi_register_types)
