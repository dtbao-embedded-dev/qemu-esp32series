/*
 * ESP32-S3 GPIO emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * Models the GPIO output, output-enable and input registers. A pad whose
 * output is enabled reads back its own output level (as a real pad with
 * the input buffer enabled); any other pad reads the level driven from
 * outside, set through the "in" property (bit n = GPIOn) or the "pin"
 * qdev input lines. IO_MUX and the GPIO matrix routing are not modelled:
 * FUNCn_IN_SEL/FUNCn_OUT_SEL only keep the value written to them.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/esp32s3_gpio.h"

#define ESP32S3_GPIO_PIN_END \
    (A_GPIO_PIN0 + (ESP32S3_GPIO_PIN_REG_COUNT - 1) * 4)
#define ESP32S3_GPIO_FUNC_IN_END \
    (A_GPIO_FUNC0_IN_SEL + (ESP32S3_GPIO_FUNC_IN_COUNT - 1) * 4)
#define ESP32S3_GPIO_FUNC_OUT_END \
    (A_GPIO_FUNC0_OUT_SEL + (ESP32S3_GPIO_PIN_REG_COUNT - 1) * 4)

/* Current level of every pad, bit n = GPIOn */
static uint64_t esp32s3_gpio_levels(ESP32S3GPIOState *s)
{
    uint64_t out = s->out[0] | ((uint64_t)s->out[1] << 32);
    uint64_t enable = s->enable[0] | ((uint64_t)s->enable[1] << 32);

    return ((out & enable) | (s->ext & ~enable)) & ESP32S3_GPIO_PIN_MASK;
}

/* Called after anything that can change a pad level */
static void esp32s3_gpio_update(ESP32S3GPIOState *s)
{
}

static uint64_t esp32s3_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3GPIOState *s = ESP32S3_GPIO(opaque);
    uint64_t r = 0;

    switch (addr) {
    case A_GPIO_BT_SELECT:
        r = s->bt_select;
        break;
    case A_GPIO_OUT:
        r = s->out[0];
        break;
    case A_GPIO_OUT1:
        r = s->out[1];
        break;
    case A_GPIO_SDIO_SELECT:
        r = s->sdio_select;
        break;
    case A_GPIO_ENABLE:
        r = s->enable[0];
        break;
    case A_GPIO_ENABLE1:
        r = s->enable[1];
        break;
    case A_GPIO_STRAP:
        r = s->strap_mode;
        break;
    case A_GPIO_IN:
        r = (uint32_t)esp32s3_gpio_levels(s);
        break;
    case A_GPIO_IN1:
        r = esp32s3_gpio_levels(s) >> 32;
        break;
    case A_GPIO_PIN0 ... ESP32S3_GPIO_PIN_END:
        r = s->pin[(addr - A_GPIO_PIN0) / 4];
        break;
    case A_GPIO_FUNC0_IN_SEL ... ESP32S3_GPIO_FUNC_IN_END:
        r = s->func_in_sel[(addr - A_GPIO_FUNC0_IN_SEL) / 4];
        break;
    case A_GPIO_FUNC0_OUT_SEL ... ESP32S3_GPIO_FUNC_OUT_END:
        r = s->func_out_sel[(addr - A_GPIO_FUNC0_OUT_SEL) / 4];
        break;
    case A_GPIO_CLOCK_GATE:
        r = s->clock_gate;
        break;
    case A_GPIO_DATE:
        r = ESP32S3_GPIO_DATE_VALUE;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read at 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    return r;
}

static void esp32s3_gpio_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned int size)
{
    ESP32S3GPIOState *s = ESP32S3_GPIO(opaque);
    uint32_t v = value;

    switch (addr) {
    case A_GPIO_BT_SELECT:
        s->bt_select = v;
        break;
    case A_GPIO_OUT:
        s->out[0] = v;
        break;
    case A_GPIO_OUT_W1TS:
        s->out[0] |= v;
        break;
    case A_GPIO_OUT_W1TC:
        s->out[0] &= ~v;
        break;
    case A_GPIO_OUT1:
        s->out[1] = v & ESP32S3_GPIO_BANK1_MASK;
        break;
    case A_GPIO_OUT1_W1TS:
        s->out[1] |= v & ESP32S3_GPIO_BANK1_MASK;
        break;
    case A_GPIO_OUT1_W1TC:
        s->out[1] &= ~v;
        break;
    case A_GPIO_SDIO_SELECT:
        s->sdio_select = v & 0xff;
        break;
    case A_GPIO_ENABLE:
        s->enable[0] = v;
        break;
    case A_GPIO_ENABLE_W1TS:
        s->enable[0] |= v;
        break;
    case A_GPIO_ENABLE_W1TC:
        s->enable[0] &= ~v;
        break;
    case A_GPIO_ENABLE1:
        s->enable[1] = v & ESP32S3_GPIO_BANK1_MASK;
        break;
    case A_GPIO_ENABLE1_W1TS:
        s->enable[1] |= v & ESP32S3_GPIO_BANK1_MASK;
        break;
    case A_GPIO_ENABLE1_W1TC:
        s->enable[1] &= ~v;
        break;
    case A_GPIO_PIN0 ... ESP32S3_GPIO_PIN_END:
        s->pin[(addr - A_GPIO_PIN0) / 4] = v;
        break;
    case A_GPIO_FUNC0_IN_SEL ... ESP32S3_GPIO_FUNC_IN_END:
        s->func_in_sel[(addr - A_GPIO_FUNC0_IN_SEL) / 4] = v;
        break;
    case A_GPIO_FUNC0_OUT_SEL ... ESP32S3_GPIO_FUNC_OUT_END:
        s->func_out_sel[(addr - A_GPIO_FUNC0_OUT_SEL) / 4] = v;
        break;
    case A_GPIO_CLOCK_GATE:
        s->clock_gate = v & 1;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at 0x%" HWADDR_PRIx
                      " = 0x%" PRIx32 "\n", __func__, addr, v);
        return;
    }
    esp32s3_gpio_update(s);
}

static const MemoryRegionOps esp32s3_gpio_ops = {
    .read =  esp32s3_gpio_read,
    .write = esp32s3_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

/* qdev input line n drives GPIOn from another QEMU device */
static void esp32s3_gpio_set_pin(void *opaque, int n, int level)
{
    ESP32S3GPIOState *s = ESP32S3_GPIO(opaque);

    if (level) {
        s->ext |= 1ULL << n;
    } else {
        s->ext &= ~(1ULL << n);
    }
    esp32s3_gpio_update(s);
}

static void esp32s3_gpio_get_in(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    ESP32S3GPIOState *s = ESP32S3_GPIO(obj);

    visit_type_uint64(v, name, &s->ext, errp);
}

static void esp32s3_gpio_set_in(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    ESP32S3GPIOState *s = ESP32S3_GPIO(obj);
    uint64_t value;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }
    s->ext = value & ESP32S3_GPIO_PIN_MASK;
    esp32s3_gpio_update(s);
}

static void esp32s3_gpio_reset_hold(Object *obj, ResetType type)
{
    ESP32S3GPIOState *s = ESP32S3_GPIO(obj);

    /* The externally driven levels (s->ext) belong to the board, keep them */
    memset(s->out, 0, sizeof(s->out));
    memset(s->enable, 0, sizeof(s->enable));
    s->bt_select = 0;
    s->sdio_select = 0;
    memset(s->pin, 0, sizeof(s->pin));
    memset(s->func_in_sel, 0, sizeof(s->func_in_sel));
    for (int i = 0; i < ESP32S3_GPIO_PIN_REG_COUNT; i++) {
        s->func_out_sel[i] = ESP32S3_GPIO_FUNC_OUT_SEL_DEFAULT;
    }
    s->clock_gate = ESP32S3_GPIO_CLOCK_GATE_DEFAULT;
    esp32s3_gpio_update(s);
}

static void esp32s3_gpio_init(Object *obj)
{
    ESP32S3GPIOState *s = ESP32S3_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32s3_gpio_ops, s,
                          TYPE_ESP32S3_GPIO, ESP32S3_GPIO_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), esp32s3_gpio_set_pin,
                            ESP32S3_GPIO_PIN_IN, ESP32S3_GPIO_PIN_COUNT);

    /* Not a qdev property, so that qom-set can change it at run time */
    object_property_add(obj, "in", "uint64", esp32s3_gpio_get_in,
                        esp32s3_gpio_set_in, NULL, NULL);
    object_property_set_description(obj, "in",
                                    "Levels driven on the pads from outside, bit n = GPIOn");
}

static Property esp32s3_gpio_properties[] = {
    DEFINE_PROP_UINT32("strap_mode", ESP32S3GPIOState, strap_mode,
                       ESP32S3_STRAP_MODE_FLASH_BOOT),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32s3_gpio_reset_hold;
    device_class_set_props(dc, esp32s3_gpio_properties);
}

static const TypeInfo esp32s3_gpio_info = {
    .name = TYPE_ESP32S3_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3GPIOState),
    .instance_init = esp32s3_gpio_init,
    .class_init = esp32s3_gpio_class_init,
    .class_size = sizeof(ESP32S3GPIOClass),
};

static void esp32s3_gpio_register_types(void)
{
    type_register_static(&esp32s3_gpio_info);
}

type_init(esp32s3_gpio_register_types)
