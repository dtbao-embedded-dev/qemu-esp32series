/*
 * ESP32-S3 LED PWM controller (LEDC) emulation
 *
 * The register map differs from the ESP32 one (hw/misc/esp32_ledc.c): the
 * ESP32-S3 only has the low-speed group, with 8 channels and 4 timers.
 *
 * A duty update follows the driver sequence: write DUTY and CONF1 with
 * DUTY_START set, then set PARA_UP in CONF0. On PARA_UP the duty is copied
 * to DUTY_R and the channel LED intensity is set to duty / 2^DUTY_RES of
 * the timer selected by TIMER_SEL. PARA_UP and DUTY_START self-clear.
 *
 * A fade (DUTY_NUM steps of DUTY_SCALE, up if DUTY_INC) completes at once:
 * DUTY_R gets the final duty and DUTY_CHNG_END_CHn is raised. The ESP-IDF
 * driver reads DUTY_R in its interrupt handler and starts another fade
 * until the target is reached, so the final duty must be exact. Plain duty
 * updates are fades of zero scale and raise the interrupt too.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/misc/esp32s3_ledc.h"

static void esp32s3_ledc_update_led(ESP32S3LEDCState *s, unsigned ch)
{
    unsigned timer = FIELD_EX32(s->ch_conf0[ch], ESP32S3_LEDC_CH_CONF0, TIMER_SEL);
    unsigned res = FIELD_EX32(s->timer_conf[timer], ESP32S3_LEDC_TIMER_CONF, DUTY_RES);
    uint64_t duty = s->ch_duty_r[ch] >> ESP32S3_LEDC_DUTY_FRAC_BITS;
    unsigned percent = 0;

    if (res != 0) {
        percent = MIN((duty * 100) >> res, 100);
    }
    led_set_intensity(&s->led[ch], percent);
}

static void esp32s3_ledc_update_irq(ESP32S3LEDCState *s)
{
    qemu_set_irq(s->irq, (s->int_raw & s->int_ena) != 0);
}

/* PARA_UP was set on channel ch: apply a pending duty update or fade */
static void esp32s3_ledc_update_channel(ESP32S3LEDCState *s, unsigned ch)
{
    uint32_t conf1 = s->ch_conf1[ch];
    int64_t step, duty;

    if (!FIELD_EX32(conf1, ESP32S3_LEDC_CH_CONF1, DUTY_START)) {
        return;
    }
    s->ch_conf1[ch] = FIELD_DP32(conf1, ESP32S3_LEDC_CH_CONF1, DUTY_START, 0);

    step = (int64_t)FIELD_EX32(conf1, ESP32S3_LEDC_CH_CONF1, DUTY_SCALE) *
           FIELD_EX32(conf1, ESP32S3_LEDC_CH_CONF1, DUTY_NUM);
    step <<= ESP32S3_LEDC_DUTY_FRAC_BITS;
    duty = s->ch_duty[ch];
    duty += FIELD_EX32(conf1, ESP32S3_LEDC_CH_CONF1, DUTY_INC) ? step : -step;
    s->ch_duty_r[ch] = MIN(MAX(duty, 0), ESP32S3_LEDC_CH_DUTY_MASK);
    esp32s3_ledc_update_led(s, ch);

    s->int_raw |= 1u << (R_ESP32S3_LEDC_INT_RAW_DUTY_CHNG_END_CH0_SHIFT + ch);
    esp32s3_ledc_update_irq(s);
}

static uint64_t esp32s3_ledc_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3LEDCState *s = ESP32S3_LEDC(opaque);

    if (addr < ESP32S3_LEDC_CH_STRIDE * ESP32S3_LEDC_CHANNEL_COUNT) {
        unsigned ch = addr / ESP32S3_LEDC_CH_STRIDE;

        switch (addr % ESP32S3_LEDC_CH_STRIDE) {
        case A_ESP32S3_LEDC_CH_CONF0:
            return s->ch_conf0[ch];
        case A_ESP32S3_LEDC_CH_HPOINT:
            return s->ch_hpoint[ch];
        case A_ESP32S3_LEDC_CH_DUTY:
            return s->ch_duty[ch];
        case A_ESP32S3_LEDC_CH_CONF1:
            return s->ch_conf1[ch];
        case A_ESP32S3_LEDC_CH_DUTY_R:
            return s->ch_duty_r[ch];
        }
    }

    if (addr >= A_ESP32S3_LEDC_TIMER_CONF &&
        addr < A_ESP32S3_LEDC_TIMER_CONF +
               ESP32S3_LEDC_TIMER_STRIDE * ESP32S3_LEDC_TIMER_COUNT) {
        unsigned timer = (addr - A_ESP32S3_LEDC_TIMER_CONF) /
                         ESP32S3_LEDC_TIMER_STRIDE;

        /* The timer counter (VALUE) is not modelled and reads 0 */
        if ((addr - A_ESP32S3_LEDC_TIMER_CONF) % ESP32S3_LEDC_TIMER_STRIDE == 0) {
            return s->timer_conf[timer];
        }
        return 0;
    }

    switch (addr) {
    case A_ESP32S3_LEDC_INT_RAW:
        return s->int_raw;
    case A_ESP32S3_LEDC_INT_ST:
        return s->int_raw & s->int_ena;
    case A_ESP32S3_LEDC_INT_ENA:
        return s->int_ena;
    case A_ESP32S3_LEDC_CONF:
        return s->conf;
    case A_ESP32S3_LEDC_DATE:
        return ESP32S3_LEDC_DATE_VALUE;
    default:
        return 0;
    }
}

static void esp32s3_ledc_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned int size)
{
    ESP32S3LEDCState *s = ESP32S3_LEDC(opaque);
    uint32_t val = value;

    if (addr < ESP32S3_LEDC_CH_STRIDE * ESP32S3_LEDC_CHANNEL_COUNT) {
        unsigned ch = addr / ESP32S3_LEDC_CH_STRIDE;

        switch (addr % ESP32S3_LEDC_CH_STRIDE) {
        case A_ESP32S3_LEDC_CH_CONF0:
            s->ch_conf0[ch] = val & ESP32S3_LEDC_CH_CONF0_MASK;
            if (FIELD_EX32(val, ESP32S3_LEDC_CH_CONF0, PARA_UP)) {
                esp32s3_ledc_update_channel(s, ch);
            }
            break;
        case A_ESP32S3_LEDC_CH_HPOINT:
            s->ch_hpoint[ch] = val & ESP32S3_LEDC_CH_HPOINT_MASK;
            break;
        case A_ESP32S3_LEDC_CH_DUTY:
            s->ch_duty[ch] = val & ESP32S3_LEDC_CH_DUTY_MASK;
            break;
        case A_ESP32S3_LEDC_CH_CONF1:
            s->ch_conf1[ch] = val;
            break;
        default:
            /* DUTY_R is read-only */
            break;
        }
        return;
    }

    if (addr >= A_ESP32S3_LEDC_TIMER_CONF &&
        addr < A_ESP32S3_LEDC_TIMER_CONF +
               ESP32S3_LEDC_TIMER_STRIDE * ESP32S3_LEDC_TIMER_COUNT) {
        unsigned timer = (addr - A_ESP32S3_LEDC_TIMER_CONF) /
                         ESP32S3_LEDC_TIMER_STRIDE;

        /* PARA_UP only latches the new settings, so it is just dropped */
        if ((addr - A_ESP32S3_LEDC_TIMER_CONF) % ESP32S3_LEDC_TIMER_STRIDE == 0) {
            s->timer_conf[timer] = val & ESP32S3_LEDC_TIMER_CONF_MASK;
        }
        return;
    }

    switch (addr) {
    case A_ESP32S3_LEDC_INT_ENA:
        s->int_ena = val & ESP32S3_LEDC_INT_MASK;
        esp32s3_ledc_update_irq(s);
        break;
    case A_ESP32S3_LEDC_INT_CLR:
        s->int_raw &= ~val;
        esp32s3_ledc_update_irq(s);
        break;
    case A_ESP32S3_LEDC_CONF:
        s->conf = val & ESP32S3_LEDC_CONF_MASK;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps esp32s3_ledc_ops = {
    .read = esp32s3_ledc_read,
    .write = esp32s3_ledc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32s3_ledc_reset_hold(Object *obj, ResetType type)
{
    ESP32S3LEDCState *s = ESP32S3_LEDC(obj);

    memset(s->ch_conf0, 0, sizeof(s->ch_conf0));
    memset(s->ch_hpoint, 0, sizeof(s->ch_hpoint));
    memset(s->ch_duty, 0, sizeof(s->ch_duty));
    memset(s->ch_conf1, 0, sizeof(s->ch_conf1));
    memset(s->ch_duty_r, 0, sizeof(s->ch_duty_r));
    memset(s->timer_conf, 0, sizeof(s->timer_conf));
    s->int_raw = 0;
    s->int_ena = 0;
    s->conf = 0;
    qemu_irq_lower(s->irq);

    for (unsigned i = 0; i < ESP32S3_LEDC_CHANNEL_COUNT; i++) {
        if (led_get_intensity(&s->led[i]) != 0) {
            led_set_intensity(&s->led[i], 0);
        }
    }
}

static void esp32s3_ledc_realize(DeviceState *dev, Error **errp)
{
    ESP32S3LEDCState *s = ESP32S3_LEDC(dev);

    for (unsigned i = 0; i < ESP32S3_LEDC_CHANNEL_COUNT; i++) {
        g_autofree char *desc = g_strdup_printf("ledc.ch%u", i);
        DeviceState *led = DEVICE(&s->led[i]);

        qdev_prop_set_string(led, "color", "blue");
        qdev_prop_set_string(led, "description", desc);
        if (!qdev_realize(led, NULL, errp)) {
            return;
        }
    }
}

static void esp32s3_ledc_init(Object *obj)
{
    ESP32S3LEDCState *s = ESP32S3_LEDC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32s3_ledc_ops, s,
                          TYPE_ESP32S3_LEDC, ESP32S3_LEDC_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    for (unsigned i = 0; i < ESP32S3_LEDC_CHANNEL_COUNT; i++) {
        g_autofree char *name = g_strdup_printf("led%u", i);

        object_initialize_child(obj, name, &s->led[i], TYPE_LED);
    }
}

static void esp32s3_ledc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp32s3_ledc_realize;
    rc->phases.hold = esp32s3_ledc_reset_hold;
}

static const TypeInfo esp32s3_ledc_info = {
    .name = TYPE_ESP32S3_LEDC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3LEDCState),
    .instance_init = esp32s3_ledc_init,
    .class_init = esp32s3_ledc_class_init,
};

static void esp32s3_ledc_register_types(void)
{
    type_register_static(&esp32s3_ledc_info);
}

type_init(esp32s3_ledc_register_types)
