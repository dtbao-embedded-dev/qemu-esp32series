/*
 * ESP32-S3 LED PWM controller (LEDC) emulation
 *
 * Low-speed mode only, as on the real chip: 8 channels and 4 timers.
 * Duty updates take effect on the channel PARA_UP bit; each channel drives
 * a LED child whose intensity follows the duty cycle. Fades complete at
 * once and raise DUTY_CHNG_END. No PWM timing is modelled.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32S3_LEDC_H
#define ESP32S3_LEDC_H

#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/led.h"

#define TYPE_ESP32S3_LEDC "esp32s3.ledc"
#define ESP32S3_LEDC(obj) OBJECT_CHECK(ESP32S3LEDCState, (obj), TYPE_ESP32S3_LEDC)

#define ESP32S3_LEDC_MEM_SIZE       0x100
#define ESP32S3_LEDC_CHANNEL_COUNT  8
#define ESP32S3_LEDC_TIMER_COUNT    4
#define ESP32S3_LEDC_CH_STRIDE      0x14
#define ESP32S3_LEDC_TIMER_STRIDE   0x8
/* The DUTY and DUTY_R registers hold the duty with 4 fractional bits */
#define ESP32S3_LEDC_DUTY_FRAC_BITS 4
#define ESP32S3_LEDC_DATE_VALUE     0x19040200

/* Channel n registers are at n * ESP32S3_LEDC_CH_STRIDE from these */
REG32(ESP32S3_LEDC_CH_CONF0, 0x00)
    FIELD(ESP32S3_LEDC_CH_CONF0, TIMER_SEL, 0, 2)
    FIELD(ESP32S3_LEDC_CH_CONF0, PARA_UP, 4, 1)
REG32(ESP32S3_LEDC_CH_HPOINT, 0x04)
REG32(ESP32S3_LEDC_CH_DUTY, 0x08)
REG32(ESP32S3_LEDC_CH_CONF1, 0x0c)
    FIELD(ESP32S3_LEDC_CH_CONF1, DUTY_SCALE, 0, 10)
    FIELD(ESP32S3_LEDC_CH_CONF1, DUTY_NUM, 20, 10)
    FIELD(ESP32S3_LEDC_CH_CONF1, DUTY_INC, 30, 1)
    FIELD(ESP32S3_LEDC_CH_CONF1, DUTY_START, 31, 1)
REG32(ESP32S3_LEDC_CH_DUTY_R, 0x10)

/* Timer n registers are at n * ESP32S3_LEDC_TIMER_STRIDE from these */
REG32(ESP32S3_LEDC_TIMER_CONF, 0xa0)
    FIELD(ESP32S3_LEDC_TIMER_CONF, DUTY_RES, 0, 4)
    FIELD(ESP32S3_LEDC_TIMER_CONF, PARA_UP, 25, 1)
REG32(ESP32S3_LEDC_TIMER_VALUE, 0xa4)

/* Same bit layout in INT_RAW, INT_ST, INT_ENA and INT_CLR */
REG32(ESP32S3_LEDC_INT_RAW, 0xc0)
    FIELD(ESP32S3_LEDC_INT_RAW, DUTY_CHNG_END_CH0, 4, 1)
REG32(ESP32S3_LEDC_INT_ST, 0xc4)
REG32(ESP32S3_LEDC_INT_ENA, 0xc8)
REG32(ESP32S3_LEDC_INT_CLR, 0xcc)
REG32(ESP32S3_LEDC_CONF, 0xd0)
REG32(ESP32S3_LEDC_DATE, 0xfc)

/*
 * Bits that read back as written. The self-clearing PARA_UP and
 * OVF_CNT_RESET bits and the read-only OVF_CNT_RESET_ST are left out.
 */
#define ESP32S3_LEDC_CH_CONF0_MASK  0x0000ffefu
#define ESP32S3_LEDC_CH_HPOINT_MASK 0x00003fffu
#define ESP32S3_LEDC_CH_DUTY_MASK   0x0007ffffu
#define ESP32S3_LEDC_TIMER_CONF_MASK 0x01ffffffu
#define ESP32S3_LEDC_INT_MASK       0x000fffffu
#define ESP32S3_LEDC_CONF_MASK      0x80000003u

typedef struct ESP32S3LEDCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t ch_conf0[ESP32S3_LEDC_CHANNEL_COUNT];
    uint32_t ch_hpoint[ESP32S3_LEDC_CHANNEL_COUNT];
    uint32_t ch_duty[ESP32S3_LEDC_CHANNEL_COUNT];
    uint32_t ch_conf1[ESP32S3_LEDC_CHANNEL_COUNT];
    uint32_t ch_duty_r[ESP32S3_LEDC_CHANNEL_COUNT];
    uint32_t timer_conf[ESP32S3_LEDC_TIMER_COUNT];
    uint32_t int_raw;
    uint32_t int_ena;
    uint32_t conf;

    LEDState led[ESP32S3_LEDC_CHANNEL_COUNT];
} ESP32S3LEDCState;

#endif /* ESP32S3_LEDC_H */
