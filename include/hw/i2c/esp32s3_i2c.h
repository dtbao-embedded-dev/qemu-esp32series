/*
 * ESP32-S3 I2C controller emulation
 *
 * Subtype of the ESP32 I2C controller (hw/i2c/esp32_i2c.c) with the
 * ESP32-S3 command opcodes, command register count and extra
 * configuration registers. Master mode only.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef ESP32S3_I2C_H
#define ESP32S3_I2C_H

#include "hw/i2c/esp32_i2c.h"

#define TYPE_ESP32S3_I2C "esp32s3.i2c"
#define ESP32S3_I2C(obj) OBJECT_CHECK(ESP32S3I2CState, (obj), TYPE_ESP32S3_I2C)
#define ESP32S3_I2C_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32S3I2CClass, obj, TYPE_ESP32S3_I2C)
#define ESP32S3_I2C_CLASS(klass) OBJECT_CLASS_CHECK(ESP32S3I2CClass, klass, TYPE_ESP32S3_I2C)

/* Registers up to 0xF8 plus the TX/RX FIFO RAM windows at 0x100 and 0x180 */
#define ESP32S3_I2C_MEM_SIZE    0x200
#define ESP32S3_I2C_CMD_COUNT   8

REG32(ESP32S3_I2C_CTR, 0x04)
    FIELD(ESP32S3_I2C_CTR, FSM_RST, 10, 1)
    FIELD(ESP32S3_I2C_CTR, CONF_UPGATE, 11, 1)

REG32(ESP32S3_I2C_FIFO_CONF, 0x18)
REG32(ESP32S3_I2C_FILTER_CFG, 0x50)
REG32(ESP32S3_I2C_CLK_CONF, 0x54)
REG32(ESP32S3_I2C_SCL_ST_TIME_OUT, 0x78)
REG32(ESP32S3_I2C_SCL_MAIN_ST_TIME_OUT, 0x7c)
REG32(ESP32S3_I2C_SCL_SP_CONF, 0x80)
    FIELD(ESP32S3_I2C_SCL_SP_CONF, SCL_RST_SLV_EN, 0, 1)
REG32(ESP32S3_I2C_SCL_STRETCH_CONF, 0x84)

/* I2C_COMDn.OPCODE values on the ESP32-S3 */
typedef enum {
    ESP32S3_I2C_OPCODE_WRITE   = 1,
    ESP32S3_I2C_OPCODE_STOP    = 2,
    ESP32S3_I2C_OPCODE_READ    = 3,
    ESP32S3_I2C_OPCODE_END     = 4,
    ESP32S3_I2C_OPCODE_RSTART  = 6,
} Esp32s3I2COpcode;

typedef struct ESP32S3I2CState {
    Esp32I2CState parent;

    /* Configuration registers that only need to read back */
    uint32_t fifo_conf;
    uint32_t filter_cfg;
    uint32_t clk_conf;
    uint32_t scl_st_time_out;
    uint32_t scl_main_st_time_out;
    uint32_t scl_sp_conf;
    uint32_t scl_stretch_conf;
} ESP32S3I2CState;

typedef struct ESP32S3I2CClass {
    Esp32I2CClass parent_class;

    uint64_t (*parent_i2c_read)(void *opaque, hwaddr addr, unsigned int size);
    void (*parent_i2c_write)(void *opaque, hwaddr addr, uint64_t value, unsigned int size);
    ResettablePhases parent_phases;
} ESP32S3I2CClass;

#endif /* ESP32S3_I2C_H */
