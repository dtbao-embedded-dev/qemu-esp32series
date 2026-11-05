/*
 * GP-SPI master test for the QEMU ESP32-S3 machine.
 *
 * QEMU must be started with a W25X16 flash on SPI2 CS0 and on SPI3 CS1:
 *   -device w25x16,bus=spi2,cs=0 -device w25x16,bus=spi3,cs=1
 * SPI3 CS0 has no device behind it. Every result line starts with
 * "SPI_TEST"; the last one is "SPI_TEST RESULT PASS" or "SPI_TEST RESULT FAIL".
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FLASH_JEDEC_ID      0xef3015    /* W25X16 */
#define CMD_WRITE_ENABLE    0x06
#define CMD_PAGE_PROGRAM    0x02
#define CMD_READ            0x03
#define CMD_FAST_READ       0x0b
#define CMD_READ_JEDEC_ID   0x9f
#define SMALL_LEN           32          /* fits in the W0-W15 buffer */

typedef struct {
    const char *name;
    spi_device_handle_t dev;
    bool half_duplex;
} flash_t;

static bool s_pass = true;

static void check(bool ok)
{
    if (!ok) {
        s_pass = false;
    }
}

static void report(const char *name, const char *what, esp_err_t err, bool ok)
{
    printf("SPI_TEST %s %s %s\n", name, what,
           err != ESP_OK ? esp_err_to_name(err) : ok ? "OK" : "FAIL");
    check(err == ESP_OK && ok);
}

static bool init_bus(spi_host_device_t host, int mosi, int miso, int sclk, int dma)
{
    const spi_bus_config_t bus = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };
    esp_err_t err = spi_bus_initialize(host, &bus, dma);

    printf("SPI_TEST spi%d init %s\n", host + 1, esp_err_to_name(err));
    check(err == ESP_OK);
    return err == ESP_OK;
}

static spi_device_handle_t add_device(spi_host_device_t host, int cs, bool half_duplex)
{
    const spi_device_interface_config_t cfg = {
        .command_bits = 8,
        .address_bits = 24,
        .mode = 0,
        .clock_speed_hz = 1000000,
        .spics_io_num = cs,
        .queue_size = 4,
        .flags = half_duplex ? SPI_DEVICE_HALFDUPLEX : 0,
    };
    spi_device_handle_t dev = NULL;
    esp_err_t err = spi_bus_add_device(host, &cfg, &dev);

    if (err != ESP_OK) {
        printf("SPI_TEST spi%d add device cs gpio%d %s\n", host + 1, cs, esp_err_to_name(err));
        check(false);
    }
    return dev;
}

/* Command-only or command + data transaction without an address phase */
static esp_err_t no_addr_trans(const flash_t *f, uint8_t cmd, uint8_t *rx, size_t rx_len,
                               bool polling)
{
    spi_transaction_ext_t t = {
        .base = {
            .flags = SPI_TRANS_VARIABLE_ADDR,
            .cmd = cmd,
            .rx_buffer = rx,
        },
        .address_bits = 0,
    };

    if (f->half_duplex) {
        t.base.rxlength = rx_len * 8;
    } else {
        t.base.length = rx_len * 8;
    }
    return polling ? spi_device_polling_transmit(f->dev, &t.base)
                   : spi_device_transmit(f->dev, &t.base);
}

static uint32_t read_id(const flash_t *f, bool polling, esp_err_t *err)
{
    uint8_t id[3] = { 0 };

    *err = no_addr_trans(f, CMD_READ_JEDEC_ID, id, sizeof(id), polling);
    return (id[0] << 16) | (id[1] << 8) | id[2];
}

static esp_err_t program(const flash_t *f, uint32_t addr, const uint8_t *data, size_t len)
{
    spi_transaction_t t = {
        .cmd = CMD_PAGE_PROGRAM,
        .addr = addr,
        .length = len * 8,
        .tx_buffer = data,
    };
    esp_err_t err = no_addr_trans(f, CMD_WRITE_ENABLE, NULL, 0, false);

    return err == ESP_OK ? spi_device_transmit(f->dev, &t) : err;
}

/* Full duplex uses READ (0x03); half duplex uses FAST_READ with 8 dummy bits */
static esp_err_t read_data(const flash_t *f, uint32_t addr, uint8_t *data, size_t len)
{
    spi_transaction_ext_t t = {
        .base = {
            .cmd = f->half_duplex ? CMD_FAST_READ : CMD_READ,
            .addr = addr,
            .rx_buffer = data,
        },
    };

    if (f->half_duplex) {
        t.base.flags = SPI_TRANS_VARIABLE_DUMMY;
        t.dummy_bits = 8;
        t.base.rxlength = len * 8;
    } else {
        t.base.length = len * 8;
    }
    return spi_device_transmit(f->dev, &t.base);
}

static void test_flash(const flash_t *f, uint32_t addr, size_t len)
{
    uint8_t *tx = heap_caps_malloc(len, MALLOC_CAP_DMA);
    uint8_t *rx = heap_caps_calloc(1, len, MALLOC_CAP_DMA);
    char what[48];
    esp_err_t err;
    uint32_t id;

    for (size_t i = 0; i < len; i++) {
        tx[i] = (uint8_t)(i * 7 + addr / 256 + 3);
    }

    id = read_id(f, true, &err);
    snprintf(what, sizeof(what), "jedec polling 0x%06lx", (unsigned long)id);
    report(f->name, what, err, id == FLASH_JEDEC_ID);
    id = read_id(f, false, &err);
    snprintf(what, sizeof(what), "jedec queued 0x%06lx", (unsigned long)id);
    report(f->name, what, err, id == FLASH_JEDEC_ID);

    err = program(f, addr, tx, len);
    snprintf(what, sizeof(what), "program %u bytes at 0x%lx", (unsigned)len, (unsigned long)addr);
    report(f->name, what, err, true);

    err = read_data(f, addr, rx, len);
    bool same = memcmp(tx, rx, len) == 0;
    snprintf(what, sizeof(what), "read back %u bytes [0]=0x%02x [%u]=0x%02x", (unsigned)len,
             rx[0], (unsigned)len - 1, rx[len - 1]);
    report(f->name, what, err, same);
    if (!same) {
        printf("SPI_TEST %s rx[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x\n", f->name,
               rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);
    }

    free(tx);
    free(rx);
}

void app_main(void)
{
    printf("SPI_TEST START\n");

    if (init_bus(SPI2_HOST, 11, 13, 12, SPI_DMA_DISABLED)) {
        const flash_t f = { "spi2-cs0", add_device(SPI2_HOST, 10, false), false };
        if (f.dev) {
            test_flash(&f, 0x100, SMALL_LEN);
        }
    }

    if (init_bus(SPI3_HOST, 4, 5, 6, SPI_DMA_DISABLED)) {
        const flash_t empty = { "spi3-cs0", add_device(SPI3_HOST, 7, false), false };
        const flash_t f = { "spi3-cs1", add_device(SPI3_HOST, 8, true), true };
        esp_err_t err;

        if (empty.dev) {
            uint32_t id = read_id(&empty, true, &err);
            char what[32];
            snprintf(what, sizeof(what), "no device id 0x%06lx", (unsigned long)id);
            report(empty.name, what, err, id == 0 || id == 0xffffff);
        }
        if (f.dev) {
            test_flash(&f, 0x200, SMALL_LEN);
        }
    }

    printf("SPI_TEST RESULT %s\n", s_pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
