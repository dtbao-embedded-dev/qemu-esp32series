/*
 * GPIO test for the QEMU ESP32-S3 machine.
 *
 * QEMU must be started with GPIO5 and GPIO41 driven high from outside:
 *   -global driver=esp32s3.gpio,property=in,value=2199023255584
 * (bit 5 | bit 41). After "GPIO_TEST ready" the runner drives GPIO6 and
 * GPIO42 high instead (bit 6 | bit 42) from the monitor:
 *   qom-set /machine/soc/gpio in 4398046511168
 * Every result line starts with "GPIO_TEST"; the last one is
 * "GPIO_TEST RESULT PASS" or "GPIO_TEST RESULT FAIL".
 */

#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define QOM_SET_TIMEOUT_MS  20000

/* Output pins read back through the pad (one per GPIO bank) */
static const gpio_num_t s_loop_pins[] = { GPIO_NUM_4, GPIO_NUM_40 };
/* Input pins driven by QEMU */
static const gpio_num_t s_in_pins[] = { GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_41, GPIO_NUM_42 };

static bool s_pass = true;

static void check(bool ok)
{
    if (!ok) {
        s_pass = false;
    }
}

static void config_pin(gpio_num_t pin, gpio_mode_t mode)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);

    if (err != ESP_OK) {
        printf("GPIO_TEST config gpio%d %s\n", pin, esp_err_to_name(err));
        check(false);
    }
}

static void test_loopback(void)
{
    for (size_t i = 0; i < sizeof(s_loop_pins) / sizeof(s_loop_pins[0]); i++) {
        gpio_num_t pin = s_loop_pins[i];
        int high, low;

        config_pin(pin, GPIO_MODE_INPUT_OUTPUT);
        gpio_set_level(pin, 1);
        high = gpio_get_level(pin);
        gpio_set_level(pin, 0);
        low = gpio_get_level(pin);
        bool ok = high == 1 && low == 0;
        printf("GPIO_TEST loopback gpio%d high=%d low=%d %s\n", pin, high, low,
               ok ? "OK" : "FAIL");
        check(ok);
    }
}

/* Return a bit mask of the s_in_pins levels, bit i for s_in_pins[i] */
static unsigned read_inputs(void)
{
    unsigned mask = 0;

    for (size_t i = 0; i < sizeof(s_in_pins) / sizeof(s_in_pins[0]); i++) {
        if (gpio_get_level(s_in_pins[i])) {
            mask |= 1u << i;
        }
    }
    return mask;
}

static void test_inputs(void)
{
    /* GPIO5 and GPIO41 high from -global */
    const unsigned boot_mask = 0x5;
    /* GPIO6 and GPIO42 high after the qom-set */
    const unsigned qom_mask = 0xa;
    unsigned mask;

    for (size_t i = 0; i < sizeof(s_in_pins) / sizeof(s_in_pins[0]); i++) {
        config_pin(s_in_pins[i], GPIO_MODE_INPUT);
    }
    mask = read_inputs();
    printf("GPIO_TEST inputs at boot mask=0x%x %s\n", mask, mask == boot_mask ? "OK" : "FAIL");
    check(mask == boot_mask);

    printf("GPIO_TEST ready\n");
    for (int t = 0; t < QOM_SET_TIMEOUT_MS / 100 && mask != qom_mask; t++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        mask = read_inputs();
    }
    printf("GPIO_TEST inputs after qom-set mask=0x%x %s\n", mask,
           mask == qom_mask ? "OK" : "TIMEOUT");
    check(mask == qom_mask);
}

void app_main(void)
{
    printf("GPIO_TEST START\n");
    test_loopback();
    test_inputs();

    printf("GPIO_TEST RESULT %s\n", s_pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
