/*
 * ESP32-S3 I2C controller emulation
 *
 * This implementation overrides the ESP32 I2C one (hw/i2c/esp32_i2c.c),
 * check it out first. The transaction engine, FIFOs and interrupt bits
 * (NACK 10, TRANS_COMPLETE 7, END_DETECT 3) are shared; the ESP32-S3 has
 * different command opcodes, 8 command registers and a few extra registers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/esp32s3_i2c.h"

static uint64_t esp32s3_i2c_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3I2CClass *class = ESP32S3_I2C_GET_CLASS(opaque);
    ESP32S3I2CState *s = ESP32S3_I2C(opaque);

    switch (addr) {
    case A_ESP32S3_I2C_FIFO_CONF:
        return s->fifo_conf;
    case A_ESP32S3_I2C_FILTER_CFG:
        return s->filter_cfg;
    case A_ESP32S3_I2C_CLK_CONF:
        return s->clk_conf;
    case A_ESP32S3_I2C_SCL_ST_TIME_OUT:
        return s->scl_st_time_out;
    case A_ESP32S3_I2C_SCL_MAIN_ST_TIME_OUT:
        return s->scl_main_st_time_out;
    case A_ESP32S3_I2C_SCL_SP_CONF:
        return s->scl_sp_conf;
    case A_ESP32S3_I2C_SCL_STRETCH_CONF:
        return s->scl_stretch_conf;
    default:
        return class->parent_i2c_read(opaque, addr, size);
    }
}

static void esp32s3_i2c_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    ESP32S3I2CClass *class = ESP32S3_I2C_GET_CLASS(opaque);
    ESP32S3I2CState *s = ESP32S3_I2C(opaque);

    switch (addr) {
    case A_ESP32S3_I2C_CTR:
        if (FIELD_EX32(value, ESP32S3_I2C_CTR, FSM_RST)) {
            /* Reset the master state machine: release the bus */
            if (s->parent.trans_ongoing) {
                i2c_end_transfer(s->parent.bus);
                s->parent.trans_ongoing = false;
            }
        }
        /* FSM_RST and CONF_UPGATE are self-clearing, the timing
         * configuration they latch is not emulated */
        value &= ~(R_ESP32S3_I2C_CTR_FSM_RST_MASK |
                   R_ESP32S3_I2C_CTR_CONF_UPGATE_MASK);
        class->parent_i2c_write(opaque, addr, value, size);
        break;
    case A_ESP32S3_I2C_FIFO_CONF:
        s->fifo_conf = value;
        /* The parent handles the TX/RX FIFO reset bits */
        class->parent_i2c_write(opaque, addr, value, size);
        break;
    case A_ESP32S3_I2C_FILTER_CFG:
        s->filter_cfg = value;
        break;
    case A_ESP32S3_I2C_CLK_CONF:
        s->clk_conf = value;
        break;
    case A_ESP32S3_I2C_SCL_ST_TIME_OUT:
        s->scl_st_time_out = value;
        break;
    case A_ESP32S3_I2C_SCL_MAIN_ST_TIME_OUT:
        s->scl_main_st_time_out = value;
        break;
    case A_ESP32S3_I2C_SCL_SP_CONF:
        /* The SCL pulses that free a stuck slave complete at once */
        s->scl_sp_conf = value & ~R_ESP32S3_I2C_SCL_SP_CONF_SCL_RST_SLV_EN_MASK;
        break;
    case A_ESP32S3_I2C_SCL_STRETCH_CONF:
        s->scl_stretch_conf = value;
        break;
    default:
        class->parent_i2c_write(opaque, addr, value, size);
        break;
    }
}

static void esp32s3_i2c_reset_hold(Object *obj, ResetType type)
{
    ESP32S3I2CClass *class = ESP32S3_I2C_GET_CLASS(obj);
    ESP32S3I2CState *s = ESP32S3_I2C(obj);

    if (class->parent_phases.hold) {
        class->parent_phases.hold(obj, type);
    }

    s->fifo_conf = 0;
    s->filter_cfg = 0;
    s->clk_conf = 0;
    s->scl_st_time_out = 0;
    s->scl_main_st_time_out = 0;
    s->scl_sp_conf = 0;
    s->scl_stretch_conf = 0;
}

static void esp32s3_i2c_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ESP32S3I2CClass *esp32s3 = ESP32S3_I2C_CLASS(klass);
    Esp32I2CClass *esp32 = ESP32_I2C_CLASS(klass);

    resettable_class_set_parent_phases(rc, NULL, esp32s3_i2c_reset_hold, NULL,
                                       &esp32s3->parent_phases);

    esp32->cmd_count = ESP32S3_I2C_CMD_COUNT;
    esp32->mem_size = ESP32S3_I2C_MEM_SIZE;
    /* The class was copied from the parent, drop the ESP32 opcodes */
    memset(esp32->opcode_map, 0, sizeof(esp32->opcode_map));
    esp32->opcode_map[ESP32S3_I2C_OPCODE_RSTART] = ESP32_I2C_OP_RSTART;
    esp32->opcode_map[ESP32S3_I2C_OPCODE_WRITE] = ESP32_I2C_OP_WRITE;
    esp32->opcode_map[ESP32S3_I2C_OPCODE_READ] = ESP32_I2C_OP_READ;
    esp32->opcode_map[ESP32S3_I2C_OPCODE_STOP] = ESP32_I2C_OP_STOP;
    esp32->opcode_map[ESP32S3_I2C_OPCODE_END] = ESP32_I2C_OP_END;

    esp32s3->parent_i2c_read = esp32->i2c_read;
    esp32s3->parent_i2c_write = esp32->i2c_write;
    esp32->i2c_read = esp32s3_i2c_read;
    esp32->i2c_write = esp32s3_i2c_write;
}

static const TypeInfo esp32s3_i2c_info = {
    .name = TYPE_ESP32S3_I2C,
    .parent = TYPE_ESP32_I2C,
    .instance_size = sizeof(ESP32S3I2CState),
    .class_init = esp32s3_i2c_class_init,
    .class_size = sizeof(ESP32S3I2CClass),
};

static void esp32s3_i2c_register_types(void)
{
    type_register_static(&esp32s3_i2c_info);
}

type_init(esp32s3_i2c_register_types)
