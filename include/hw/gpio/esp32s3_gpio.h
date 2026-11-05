#pragma once

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/registerfields.h"

#define TYPE_ESP32S3_GPIO "esp32s3.gpio"
#define ESP32S3_GPIO(obj)           OBJECT_CHECK(ESP32S3GPIOState, (obj), TYPE_ESP32S3_GPIO)
#define ESP32S3_GPIO_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32S3GPIOClass, obj, TYPE_ESP32S3_GPIO)
#define ESP32S3_GPIO_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32S3GPIOClass, klass, TYPE_ESP32S3_GPIO)

/* Bootstrap options for ESP32-S3 (4-bit) */
#define ESP32S3_STRAP_MODE_FLASH_BOOT 0x4   /* SPI Boot */

/* GPIO0..GPIO48 exist; the register file has PIN/FUNC_OUT_SEL slots up to 53 */
#define ESP32S3_GPIO_PIN_COUNT      49
#define ESP32S3_GPIO_PIN_MASK       ((1ULL << ESP32S3_GPIO_PIN_COUNT) - 1)
#define ESP32S3_GPIO_PIN_REG_COUNT  54
#define ESP32S3_GPIO_FUNC_IN_COUNT  256
#define ESP32S3_GPIO_IO_SIZE        0x1000

/* Name of the qdev input lines that drive the pad levels from other devices */
#define ESP32S3_GPIO_PIN_IN         "pin"

REG32(GPIO_BT_SELECT,       0x0000)
REG32(GPIO_OUT,             0x0004)
REG32(GPIO_OUT_W1TS,        0x0008)
REG32(GPIO_OUT_W1TC,        0x000C)
REG32(GPIO_OUT1,            0x0010)
REG32(GPIO_OUT1_W1TS,       0x0014)
REG32(GPIO_OUT1_W1TC,       0x0018)
REG32(GPIO_SDIO_SELECT,     0x001C)
REG32(GPIO_ENABLE,          0x0020)
REG32(GPIO_ENABLE_W1TS,     0x0024)
REG32(GPIO_ENABLE_W1TC,     0x0028)
REG32(GPIO_ENABLE1,         0x002C)
REG32(GPIO_ENABLE1_W1TS,    0x0030)
REG32(GPIO_ENABLE1_W1TC,    0x0034)
REG32(GPIO_STRAP,           0x0038)
REG32(GPIO_IN,              0x003C)
REG32(GPIO_IN1,             0x0040)
REG32(GPIO_PIN0,            0x0074)
REG32(GPIO_FUNC0_IN_SEL,    0x0154)
REG32(GPIO_FUNC0_OUT_SEL,   0x0554)
REG32(GPIO_CLOCK_GATE,      0x062C)
REG32(GPIO_DATE,            0x06FC)

/* Bank 1 registers (OUT1, ENABLE1, IN1, ...): 22 bits, GPIO32 in bit 0 */
#define ESP32S3_GPIO_BANK1_MASK     0x3fffff

/* Reset values from the ESP-IDF register description (soc/gpio_reg.h) */
/* FUNCn_OUT_SEL 0x100: GPIO_OUT/GPIO_ENABLE drive the pad */
#define ESP32S3_GPIO_FUNC_OUT_SEL_DEFAULT   0x100
#define ESP32S3_GPIO_CLOCK_GATE_DEFAULT     0x1
#define ESP32S3_GPIO_DATE_VALUE             0x1907040

typedef struct ESP32S3GPIOState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t strap_mode;

    /* Output and output-enable, bank 0 (GPIO0-31) and bank 1 (GPIO32-48) */
    uint32_t out[2];
    uint32_t enable[2];
    /* Pad levels driven from outside QEMU's CPU (property "in", "pin" lines) */
    uint64_t ext;

    uint32_t bt_select;
    uint32_t sdio_select;
    uint32_t pin[ESP32S3_GPIO_PIN_REG_COUNT];
    uint32_t func_in_sel[ESP32S3_GPIO_FUNC_IN_COUNT];
    uint32_t func_out_sel[ESP32S3_GPIO_PIN_REG_COUNT];
    uint32_t clock_gate;
} ESP32S3GPIOState;

typedef struct ESP32S3GPIOClass {
    SysBusDeviceClass parent_class;
} ESP32S3GPIOClass;
