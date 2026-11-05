/*
 * ESP32-S3 SENS (RTC ADC and temperature sensor) emulation
 *
 * Models the RTC controller side of SAR ADC1 and ADC2, which ESP-IDF's
 * adc_oneshot driver uses, and the temperature sensor register. A
 * conversion completes at once and returns the value configured for the
 * selected channel:
 *   -global driver=esp32s3.sens,property=adc1-ch3,value=2048
 * or at run time from the monitor:
 *   qom-set /machine/soc/sens adc1-ch3 2048
 * Values are 12-bit (0-4095). ADC continuous mode (APB_SARADC), touch and
 * the ULP are not modelled.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32S3_SENS_H
#define ESP32S3_SENS_H

#include "hw/sysbus.h"
#include "hw/registerfields.h"

#define TYPE_ESP32S3_SENS "esp32s3.sens"
#define ESP32S3_SENS(obj) OBJECT_CHECK(ESP32S3SensState, (obj), TYPE_ESP32S3_SENS)

#define ESP32S3_SENS_MEM_SIZE       0x400
#define ESP32S3_SENS_ADC_CHANNELS   10
#define ESP32S3_SENS_ADC_MAX        0xfff

/* SAR_MEAS1_CTRL2 and SAR_MEAS2_CTRL2 share this layout */
REG32(ESP32S3_SENS_SAR_MEAS1_CTRL2, 0x0c)
    FIELD(ESP32S3_SENS_SAR_MEAS_CTRL2, DATA, 0, 16)
    FIELD(ESP32S3_SENS_SAR_MEAS_CTRL2, DONE, 16, 1)
    FIELD(ESP32S3_SENS_SAR_MEAS_CTRL2, START, 17, 1)
    FIELD(ESP32S3_SENS_SAR_MEAS_CTRL2, EN_PAD, 19, 12)
REG32(ESP32S3_SENS_SAR_MEAS2_CTRL2, 0x30)
REG32(ESP32S3_SENS_SAR_SLAVE_ADDR1, 0x40)
    FIELD(ESP32S3_SENS_SAR_SLAVE_ADDR1, MEAS_STATUS, 22, 8)
REG32(ESP32S3_SENS_SAR_TSENS_CTRL, 0x50)
    FIELD(ESP32S3_SENS_SAR_TSENS_CTRL, OUT, 0, 8)
    FIELD(ESP32S3_SENS_SAR_TSENS_CTRL, READY, 8, 1)

/* Raw temperature sensor reading, about 25 degrees C */
#define ESP32S3_SENS_TSENS_RAW      0x68

typedef struct ESP32S3SensState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[ESP32S3_SENS_MEM_SIZE / 4];

    /* Input value of each channel, settable as QOM properties */
    uint16_t adc[2][ESP32S3_SENS_ADC_CHANNELS];
} ESP32S3SensState;

#endif /* ESP32S3_SENS_H */
