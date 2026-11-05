/*
 * ESP32-S3 SENS (RTC ADC and temperature sensor) emulation
 *
 * See include/hw/misc/esp32s3_sens.h. A one-shot conversion follows the
 * ESP-IDF sequence: select one channel in SAR_MEASn_CTRL2.EN_PAD, clear
 * then set START, poll DONE, read DATA. Setting START completes the
 * conversion at once; clearing it clears DONE. DATA holds the configured
 * value of the selected channel, or 0 when no single channel is selected
 * (ESP-IDF self-calibration reads with all pads disabled). The ADC2 data
 * flag bits stay 0, which the driver takes as a valid arbiter result.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "hw/misc/esp32s3_sens.h"

/* Bits of SAR_MEASn_CTRL2 that software writes; DATA and DONE are status */
#define MEAS_CTRL2_WRITE_MASK \
    (~(R_ESP32S3_SENS_SAR_MEAS_CTRL2_DATA_MASK | R_ESP32S3_SENS_SAR_MEAS_CTRL2_DONE_MASK))

static void esp32s3_sens_meas_write(ESP32S3SensState *s, unsigned unit,
                                    hwaddr addr, uint32_t value)
{
    uint32_t reg = (s->regs[addr / 4] & ~MEAS_CTRL2_WRITE_MASK) |
                   (value & MEAS_CTRL2_WRITE_MASK);
    uint32_t pads = FIELD_EX32(value, ESP32S3_SENS_SAR_MEAS_CTRL2, EN_PAD);
    uint32_t data = 0;

    if (!FIELD_EX32(value, ESP32S3_SENS_SAR_MEAS_CTRL2, START)) {
        reg = FIELD_DP32(reg, ESP32S3_SENS_SAR_MEAS_CTRL2, DONE, 0);
    } else if (!FIELD_EX32(s->regs[addr / 4], ESP32S3_SENS_SAR_MEAS_CTRL2, START) ||
               !FIELD_EX32(reg, ESP32S3_SENS_SAR_MEAS_CTRL2, DONE)) {
        /* START rising (or set again before completion): convert now */
        if (is_power_of_2(pads) && ctz32(pads) < ESP32S3_SENS_ADC_CHANNELS) {
            data = s->adc[unit][ctz32(pads)] & ESP32S3_SENS_ADC_MAX;
        }
        reg = FIELD_DP32(reg, ESP32S3_SENS_SAR_MEAS_CTRL2, DATA, data);
        reg = FIELD_DP32(reg, ESP32S3_SENS_SAR_MEAS_CTRL2, DONE, 1);
    }
    s->regs[addr / 4] = reg;
}

static uint64_t esp32s3_sens_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3SensState *s = ESP32S3_SENS(opaque);
    uint32_t r = s->regs[addr / 4];

    switch (addr) {
    case A_ESP32S3_SENS_SAR_SLAVE_ADDR1:
        /* No conversion is ever in progress */
        return FIELD_DP32(r, ESP32S3_SENS_SAR_SLAVE_ADDR1, MEAS_STATUS, 0);
    case A_ESP32S3_SENS_SAR_TSENS_CTRL:
        r = FIELD_DP32(r, ESP32S3_SENS_SAR_TSENS_CTRL, OUT, ESP32S3_SENS_TSENS_RAW);
        return FIELD_DP32(r, ESP32S3_SENS_SAR_TSENS_CTRL, READY, 1);
    default:
        return r;
    }
}

static void esp32s3_sens_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32S3SensState *s = ESP32S3_SENS(opaque);

    switch (addr) {
    case A_ESP32S3_SENS_SAR_MEAS1_CTRL2:
        esp32s3_sens_meas_write(s, 0, addr, value);
        break;
    case A_ESP32S3_SENS_SAR_MEAS2_CTRL2:
        esp32s3_sens_meas_write(s, 1, addr, value);
        break;
    default:
        s->regs[addr / 4] = value;
        break;
    }
}

static const MemoryRegionOps esp32s3_sens_ops = {
    .read = esp32s3_sens_read,
    .write = esp32s3_sens_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32s3_sens_reset_hold(Object *obj, ResetType type)
{
    ESP32S3SensState *s = ESP32S3_SENS(obj);

    /* Channel inputs are configuration, not chip state: keep them */
    memset(s->regs, 0, sizeof(s->regs));
}

static void esp32s3_sens_init(Object *obj)
{
    ESP32S3SensState *s = ESP32S3_SENS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32s3_sens_ops, s,
                          TYPE_ESP32S3_SENS, ESP32S3_SENS_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /*
     * Dynamic properties, so they can be set with -global before realize and
     * with qom-set while the machine runs.
     */
    for (unsigned unit = 0; unit < 2; unit++) {
        for (unsigned ch = 0; ch < ESP32S3_SENS_ADC_CHANNELS; ch++) {
            g_autofree char *name = g_strdup_printf("adc%u-ch%u", unit + 1, ch);

            object_property_add_uint16_ptr(obj, name, &s->adc[unit][ch],
                                           OBJ_PROP_FLAG_READWRITE);
        }
    }
}

static void esp32s3_sens_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp32s3_sens_reset_hold;
}

static const TypeInfo esp32s3_sens_info = {
    .name = TYPE_ESP32S3_SENS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3SensState),
    .instance_init = esp32s3_sens_init,
    .class_init = esp32s3_sens_class_init,
};

static void esp32s3_sens_register_types(void)
{
    type_register_static(&esp32s3_sens_info);
}

type_init(esp32s3_sens_register_types)
