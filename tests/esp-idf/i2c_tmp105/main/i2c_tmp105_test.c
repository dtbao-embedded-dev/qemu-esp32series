/*
 * I2C controller test for the QEMU ESP32 / ESP32-S3 machines.
 *
 * Expects the machine's default tmp105 at 0x48 on I2C0 and no slave on
 * I2C1. Every result line starts with "I2C_TEST" so a runner can grep it;
 * the last one is "I2C_TEST RESULT PASS" or "I2C_TEST RESULT FAIL".
 * The app restarts the chip at the end, so QEMU exits with -no-reboot.
 *
 * tmp105 keeps only the upper 12 bits of T_LOW / T_HIGH, so the written
 * values have a zero low nibble.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TMP105_ADDR         0x48
#define TMP105_REG_T_LOW    0x02
#define TMP105_REG_T_HIGH   0x03
#define TMP105_T_LOW_RESET  0x4b00
#define TEST_T_LOW          0x1230
#define TEST_T_HIGH         0x2340
#define XFER_TIMEOUT_MS     100

#if CONFIG_IDF_TARGET_ESP32S3
#define I2C0_SDA_GPIO   8
#define I2C0_SCL_GPIO   9
#define I2C1_SDA_GPIO   4
#define I2C1_SCL_GPIO   5
#else
#define I2C0_SDA_GPIO   21
#define I2C0_SCL_GPIO   22
#define I2C1_SDA_GPIO   18
#define I2C1_SCL_GPIO   19
#endif

static bool s_pass = true;

static void check(bool ok)
{
    if (!ok) {
        s_pass = false;
    }
}

static esp_err_t new_bus(i2c_port_num_t port, int sda, int scl,
                         i2c_master_bus_handle_t *bus)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    return i2c_new_master_bus(&cfg, bus);
}

static void test_new_driver_i2c0(void)
{
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = new_bus(I2C_NUM_0, I2C0_SDA_GPIO, I2C0_SCL_GPIO, &bus);

    if (err != ESP_OK) {
        printf("I2C_TEST new: bus i2c0 %s\n", esp_err_to_name(err));
        check(false);
        return;
    }

    err = i2c_master_probe(bus, TMP105_ADDR, XFER_TIMEOUT_MS);
    printf("I2C_TEST new: probe i2c0 0x48 %s\n",
           (err == ESP_OK) ? "OK" : esp_err_to_name(err));
    check(err == ESP_OK);

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TMP105_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    check(err == ESP_OK);

    uint8_t reg = TMP105_REG_T_LOW;
    uint8_t rx[2] = { 0 };

    if (err == ESP_OK) {
        err = i2c_master_transmit_receive(dev, &reg, 1, rx, sizeof(rx),
                                          XFER_TIMEOUT_MS);
        printf("I2C_TEST new: T_LOW=0x%04x %s\n", (rx[0] << 8) | rx[1],
               esp_err_to_name(err));
        check(err == ESP_OK && ((rx[0] << 8) | rx[1]) == TMP105_T_LOW_RESET);

        const uint8_t tx[3] = { TMP105_REG_T_LOW, TEST_T_LOW >> 8,
                                TEST_T_LOW & 0xff };
        err = i2c_master_transmit(dev, tx, sizeof(tx), XFER_TIMEOUT_MS);
        rx[0] = rx[1] = 0;
        if (err == ESP_OK) {
            err = i2c_master_transmit_receive(dev, &reg, 1, rx, sizeof(rx),
                                              XFER_TIMEOUT_MS);
        }
        bool ok = (err == ESP_OK) && ((rx[0] << 8) | rx[1]) == TEST_T_LOW;
        printf("I2C_TEST new: write/read T_LOW=0x%04x %s\n",
               (rx[0] << 8) | rx[1], ok ? "OK" : esp_err_to_name(err));
        check(ok);
        i2c_master_bus_rm_device(dev);
    }
    i2c_del_master_bus(bus);
}

static void test_new_driver_i2c1_empty(void)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = new_bus(I2C_NUM_1, I2C1_SDA_GPIO, I2C1_SCL_GPIO, &bus);

    if (err != ESP_OK) {
        printf("I2C_TEST new: bus i2c1 %s\n", esp_err_to_name(err));
        check(false);
        return;
    }

    err = i2c_master_probe(bus, TMP105_ADDR, XFER_TIMEOUT_MS);
    printf("I2C_TEST new: probe i2c1 0x48 %s\n",
           (err == ESP_ERR_NOT_FOUND) ? "NOT_FOUND" : esp_err_to_name(err));
    check(err == ESP_ERR_NOT_FOUND);
    i2c_del_master_bus(bus);
}

/*
 * One command link with 13 hardware commands: write T_HIGH, then read it
 * back after two repeated starts. The controller holds fewer commands than
 * that, so the driver must reload the list after an END.
 */
static void test_legacy_long_cmd(void)
{
    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C0_SDA_GPIO,
        .scl_io_num = I2C0_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    uint8_t rx[2] = { 0 };
    esp_err_t err = i2c_param_config(I2C_NUM_0, &cfg);

    if (err == ESP_OK) {
        err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    }
    if (err != ESP_OK) {
        printf("I2C_TEST legacy: install %s\n", esp_err_to_name(err));
        check(false);
        return;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TMP105_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, TMP105_REG_T_HIGH, true);
    i2c_master_write_byte(cmd, TEST_T_HIGH >> 8, true);
    i2c_master_write_byte(cmd, TEST_T_HIGH & 0xff, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TMP105_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, TMP105_REG_T_HIGH, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TMP105_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &rx[0], I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &rx[1], I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(XFER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    bool ok = (err == ESP_OK) && ((rx[0] << 8) | rx[1]) == TEST_T_HIGH;
    printf("I2C_TEST legacy: long cmd (13) T_HIGH=0x%04x %s\n",
           (rx[0] << 8) | rx[1], ok ? "OK" : esp_err_to_name(err));
    check(ok);
    i2c_driver_delete(I2C_NUM_0);
}

void app_main(void)
{
    printf("I2C_TEST START\n");
    test_new_driver_i2c0();
    test_new_driver_i2c1_empty();
    test_legacy_long_cmd();
    printf("I2C_TEST RESULT %s\n", s_pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
