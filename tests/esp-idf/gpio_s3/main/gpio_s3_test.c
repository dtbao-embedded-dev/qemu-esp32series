/*
 * GPIO test for the QEMU ESP32-S3 machine.
 *
 * QEMU must be started with GPIO5 and GPIO41 driven high from outside:
 *   -global driver=esp32s3.gpio,property=in,value=2199023255584
 * (bit 5 | bit 41). After "GPIO_TEST ready" the runner drives GPIO6 and
 * GPIO42 high instead (bit 6 | bit 42) from the monitor:
 *   qom-set /machine/soc/gpio in 4398046511168
 * After "GPIO_TEST irq ready" the runner also drives GPIO11 high:
 *   qom-set /machine/soc/gpio in 4398046513216
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

/* Interrupt pins: own output toggled by the app, except GPIO11 (driven by QEMU) */
#define PIN_POSEDGE     GPIO_NUM_7
#define PIN_NEGEDGE     GPIO_NUM_8
#define PIN_HIGH_LEVEL  GPIO_NUM_9
#define PIN_EXTERNAL    GPIO_NUM_11
#define PIN_ANYEDGE     GPIO_NUM_38
/* The high-level handler releases the level after this many calls */
#define LEVEL_CALLS     3

static volatile int s_irq_count[GPIO_NUM_MAX];
static bool s_pass = true;

static void check(bool ok)
{
    if (!ok) {
        s_pass = false;
    }
}

static void config_pin_intr(gpio_num_t pin, gpio_mode_t mode, gpio_int_type_t intr)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = intr,
    };
    esp_err_t err = gpio_config(&cfg);

    if (err != ESP_OK) {
        printf("GPIO_TEST config gpio%d %s\n", pin, esp_err_to_name(err));
        check(false);
    }
}

static void config_pin(gpio_num_t pin, gpio_mode_t mode)
{
    config_pin_intr(pin, mode, GPIO_INTR_DISABLE);
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

static void gpio_isr(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(intptr_t)arg;
    int n = ++s_irq_count[pin];

    /* A level interrupt keeps firing until its level goes away */
    if (pin == PIN_HIGH_LEVEL && n >= LEVEL_CALLS) {
        gpio_set_level(pin, 0);
    }
}

static void add_intr_pin(gpio_num_t pin, gpio_mode_t mode, gpio_int_type_t intr)
{
    config_pin_intr(pin, mode, intr);
    esp_err_t err = gpio_isr_handler_add(pin, gpio_isr, (void *)(intptr_t)pin);
    if (err != ESP_OK) {
        printf("GPIO_TEST isr add gpio%d %s\n", pin, esp_err_to_name(err));
        check(false);
    }
}

/* Drive an output pin through the given levels, pausing so the ISR runs */
static void drive(gpio_num_t pin, const int *levels, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        gpio_set_level(pin, levels[i]);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void expect_count(const char *what, gpio_num_t pin, int expected)
{
    int got = s_irq_count[pin];

    printf("GPIO_TEST irq %s gpio%d count=%d expected=%d %s\n", what, pin, got,
           expected, got == expected ? "OK" : "FAIL");
    check(got == expected);
}

static void test_interrupts(void)
{
    static const int up_down[] = { 1, 0 };
    static const int up[] = { 1 };
    esp_err_t err = gpio_install_isr_service(0);

    printf("GPIO_TEST isr service %s\n", esp_err_to_name(err));
    check(err == ESP_OK);

    add_intr_pin(PIN_POSEDGE, GPIO_MODE_INPUT_OUTPUT, GPIO_INTR_POSEDGE);
    add_intr_pin(PIN_NEGEDGE, GPIO_MODE_INPUT_OUTPUT, GPIO_INTR_NEGEDGE);
    add_intr_pin(PIN_ANYEDGE, GPIO_MODE_INPUT_OUTPUT, GPIO_INTR_ANYEDGE);
    add_intr_pin(PIN_HIGH_LEVEL, GPIO_MODE_INPUT_OUTPUT, GPIO_INTR_HIGH_LEVEL);
    add_intr_pin(PIN_EXTERNAL, GPIO_MODE_INPUT, GPIO_INTR_POSEDGE);

    drive(PIN_POSEDGE, up_down, 2);
    expect_count("posedge", PIN_POSEDGE, 1);
    drive(PIN_NEGEDGE, up_down, 2);
    expect_count("negedge", PIN_NEGEDGE, 1);
    drive(PIN_ANYEDGE, up_down, 2);
    expect_count("anyedge", PIN_ANYEDGE, 2);
    drive(PIN_HIGH_LEVEL, up, 1);
    expect_count("high-level", PIN_HIGH_LEVEL, LEVEL_CALLS);

    /* No interrupt once disabled */
    gpio_intr_disable(PIN_POSEDGE);
    drive(PIN_POSEDGE, up_down, 2);
    expect_count("disabled posedge", PIN_POSEDGE, 1);

    /* Edge driven from the QEMU monitor */
    printf("GPIO_TEST irq ready\n");
    for (int t = 0; t < QOM_SET_TIMEOUT_MS / 100 && s_irq_count[PIN_EXTERNAL] == 0; t++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    expect_count("external posedge", PIN_EXTERNAL, 1);
}

void app_main(void)
{
    printf("GPIO_TEST START\n");
    test_loopback();
    test_inputs();
    test_interrupts();

    printf("GPIO_TEST RESULT %s\n", s_pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
