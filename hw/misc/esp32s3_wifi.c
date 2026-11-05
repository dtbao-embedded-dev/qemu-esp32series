/*
 * ESP32-S3 WiFi MAC emulation
 *
 * See include/hw/misc/esp32s3_wifi.h. Every register keeps the last value
 * written; the TX and MAC status registers read as idle, and the timer at
 * 0x60035000 counts virtual microseconds.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/misc/esp32s3_wifi.h"

static uint64_t esp32s3_wifi_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3WifiState *s = ESP32S3_WIFI(opaque);

    switch (addr) {
    case A_ESP32S3_WIFI_STATUS:
    case A_ESP32S3_WIFI_DMA_OUT_STATUS:
        return 1;
    case A_ESP32S3_WIFI_TIME_US:
        return (uint32_t)qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    default:
        return s->mem[addr / 4];
    }
}

static void esp32s3_wifi_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32S3WifiState *s = ESP32S3_WIFI(opaque);

    s->mem[addr / 4] = value;
}

static const MemoryRegionOps esp32s3_wifi_ops = {
    .read = esp32s3_wifi_read,
    .write = esp32s3_wifi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* Frames from the network backend are dropped until the RX path exists */
static ssize_t esp32s3_wifi_receive(NetClientState *nc, const uint8_t *buf,
                                    size_t size)
{
    return size;
}

static NetClientInfo esp32s3_wifi_net_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = esp32s3_wifi_receive,
};

static void esp32s3_wifi_reset_hold(Object *obj, ResetType type)
{
    ESP32S3WifiState *s = ESP32S3_WIFI(obj);

    memset(s->mem, 0, sizeof(s->mem));
    qemu_irq_lower(s->irq);
}

static void esp32s3_wifi_realize(DeviceState *dev, Error **errp)
{
    ESP32S3WifiState *s = ESP32S3_WIFI(dev);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&esp32s3_wifi_net_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void esp32s3_wifi_init(Object *obj)
{
    ESP32S3WifiState *s = ESP32S3_WIFI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32s3_wifi_ops, s,
                          TYPE_ESP32S3_WIFI, ESP32S3_WIFI_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property esp32s3_wifi_properties[] = {
    DEFINE_NIC_PROPERTIES(ESP32S3WifiState, conf),
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
