/*
 * LEDC test for the QEMU ESP32-S3 machine.
 *
 * Every result line starts with "LEDC_TEST" so a runner can grep it; the
 * last one is "LEDC_TEST RESULT PASS" or "LEDC_TEST RESULT FAIL". The app
 * restarts the chip at the end, so QEMU exits with -no-reboot.
 *
 * Run QEMU with "-trace led_set_intensity" to see the LED outputs: CH0
 * should report 50% and CH1 25%.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TEST_FREQ_HZ        5000
#define CH0_GPIO            4
#define CH1_GPIO            5
#define CH0_DUTY            4096    /* 50% of 13 bits */
#define CH1_DUTY            256     /* 25% of 10 bits */
#define FADE_TIME_MS        200
#define FADE_TIMEOUT_MS     1000

static SemaphoreHandle_t s_fade_done;

static bool s_pass = true;

static void check(bool ok)
{
    if (!ok) {
        s_pass = false;
    }
}

static esp_err_t setup_timer(ledc_timer_t timer, ledc_timer_bit_t res)
{
    const ledc_timer_config_t cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = timer,
        .duty_resolution = res,
        .freq_hz = TEST_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    return ledc_timer_config(&cfg);
}

static esp_err_t setup_channel(ledc_channel_t ch, ledc_timer_t timer, int gpio)
{
    const ledc_channel_config_t cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = ch,
        .timer_sel = timer,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = gpio,
        .duty = 0,
        .hpoint = 0,
    };

    return ledc_channel_config(&cfg);
}

static void test_duty(ledc_channel_t ch, uint32_t duty)
{
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);

    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
    }

    uint32_t got = ledc_get_duty(LEDC_LOW_SPEED_MODE, ch);
    bool ok = (err == ESP_OK) && got == duty;

    printf("LEDC_TEST ch%d duty=%" PRIu32 " %s\n", (int) ch, got,
           ok ? "OK" : esp_err_to_name(err == ESP_OK ? ESP_FAIL : err));
    check(ok);
}

static bool IRAM_ATTR fade_end_cb(const ledc_cb_param_t *param, void *arg)
{
    BaseType_t woken = pdFALSE;

    if (param->event == LEDC_FADE_END_EVT) {
        xSemaphoreGiveFromISR(s_fade_done, &woken);
    }
    return woken == pdTRUE;
}

static bool fade_report(const char *name, esp_err_t err, bool done,
                        uint32_t target)
{
    uint32_t got = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    bool ok = (err == ESP_OK) && done && got == target;

    printf("LEDC_TEST fade %s duty=%" PRIu32 " %s\n", name, got,
           ok ? "OK" : (err != ESP_OK ? esp_err_to_name(err) :
                        (done ? "WRONG_DUTY" : "TIMEOUT")));
    check(ok);
    return ok;
}

/*
 * Fades on CH0 (13 bits). Each one must end with the fade end callback,
 * which the driver calls from the DUTY_CHNG_END interrupt. A missing
 * interrupt makes the first fade time out; the driver then still thinks
 * a fade is running, so the remaining fades are skipped instead of hanging.
 */
static void test_fade(void)
{
    ledc_cbs_t cbs = { .fade_cb = fade_end_cb };
    esp_err_t err;

    s_fade_done = xSemaphoreCreateBinary();
    err = ledc_fade_func_install(0);
    if (err == ESP_OK) {
        err = ledc_cb_register(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, &cbs, NULL);
    }
    if (err != ESP_OK) {
        printf("LEDC_TEST fade install %s\n", esp_err_to_name(err));
        check(false);
        return;
    }

    /* Time-based fade up, no wait: completion comes from the callback */
    err = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 8191,
                                  FADE_TIME_MS);
    if (err == ESP_OK) {
        err = ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                              LEDC_FADE_NO_WAIT);
    }
    bool done = (err == ESP_OK) &&
                xSemaphoreTake(s_fade_done, pdMS_TO_TICKS(FADE_TIMEOUT_MS));
    if (!fade_report("time up", err, done, 8191)) {
        return;
    }

    /* Time-based fade down, blocking until the driver sees the fade end */
    err = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0,
                                  FADE_TIME_MS);
    if (err == ESP_OK) {
        err = ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                              LEDC_FADE_WAIT_DONE);
    }
    done = (err == ESP_OK) &&
           xSemaphoreTake(s_fade_done, pdMS_TO_TICKS(FADE_TIMEOUT_MS));
    if (!fade_report("wait down", err, done, 0)) {
        return;
    }

    /*
     * Step fade 8191 -> 1000 by 7: 1027 steps, more than the 1023 one
     * hardware fade can do, plus a remainder of 2, so the driver ISR has
     * to reload the fade twice before it reaches the target.
     */
    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 8191);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    if (err == ESP_OK) {
        err = ledc_set_fade_step_and_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                           1000, 7, 1, LEDC_FADE_NO_WAIT);
    }
    done = (err == ESP_OK) &&
           xSemaphoreTake(s_fade_done, pdMS_TO_TICKS(FADE_TIMEOUT_MS));
    fade_report("step down", err, done, 1000);
}

static void test_freq(void)
{
    uint32_t freq = ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    bool ok = abs((int) freq - TEST_FREQ_HZ) <= TEST_FREQ_HZ / 50;

    printf("LEDC_TEST timer0 freq=%" PRIu32 " %s\n", freq, ok ? "OK" : "FAIL");
    check(ok);
}

/*
 * Register-level checks on the idle CH7 / TIMER3: configuration registers
 * read back, PARA_UP (timer and channel) and DUTY_START self-clear, and a
 * duty update reaches DUTY_R.
 */
#define S3_LEDC_BASE            0x60019000
#define S3_LEDC_CH_CONF0(n)     (0x00 + 0x14 * (n))
#define S3_LEDC_CH_HPOINT(n)    (0x04 + 0x14 * (n))
#define S3_LEDC_CH_DUTY(n)      (0x08 + 0x14 * (n))
#define S3_LEDC_CH_CONF1(n)     (0x0c + 0x14 * (n))
#define S3_LEDC_CH_DUTY_R(n)    (0x10 + 0x14 * (n))
#define S3_LEDC_TIMER_CONF(n)   (0xa0 + 0x08 * (n))
#define S3_LEDC_INT_ENA         0xc8
#define S3_LEDC_CONF            0xd0
#define S3_LEDC_CH_PARA_UP      (1u << 4)
#define S3_LEDC_CH_DUTY_START   (1u << 31)
#define S3_LEDC_TIMER_PARA_UP   (1u << 25)

static volatile uint32_t *s3_reg(uint32_t offset)
{
    return (volatile uint32_t *)(S3_LEDC_BASE + offset);
}

static bool s3_rw(uint32_t offset, uint32_t value, uint32_t expect)
{
    *s3_reg(offset) = value;
    uint32_t got = *s3_reg(offset);

    if (got != expect) {
        printf("LEDC_TEST regs: 0x%02" PRIx32 " wrote 0x%08" PRIx32
               " read 0x%08" PRIx32 " expected 0x%08" PRIx32 "\n",
               offset, value, got, expect);
        return false;
    }
    return true;
}

static void test_registers(void)
{
    const uint32_t timer_conf = (0x2a5u << 4) | 10u;     /* clk_div, 10 bits */
    const uint32_t conf0 = (1u << 3) | (1u << 2) | 3u;   /* idle_lv, out, timer3 */
    const uint32_t conf1 = (1u << 30) | (1u << 20) | (2u << 10) | 0u;
    const uint32_t duty = 300u << 4;
    uint32_t conf_saved = *s3_reg(S3_LEDC_CONF);
    uint32_t int_ena_saved = *s3_reg(S3_LEDC_INT_ENA);
    bool ok = true;

    ok &= s3_rw(S3_LEDC_TIMER_CONF(3), timer_conf, timer_conf);
    ok &= s3_rw(S3_LEDC_TIMER_CONF(3), timer_conf | S3_LEDC_TIMER_PARA_UP,
                timer_conf);
    ok &= s3_rw(S3_LEDC_CH_CONF0(7), conf0, conf0);
    ok &= s3_rw(S3_LEDC_CH_HPOINT(7), 0x123, 0x123);
    ok &= s3_rw(S3_LEDC_CH_DUTY(7), 0x12345, 0x12345);
    ok &= s3_rw(S3_LEDC_CH_CONF1(7), conf1, conf1);
    ok &= s3_rw(S3_LEDC_CONF, 0x80000002, 0x80000002);
    *s3_reg(S3_LEDC_CONF) = conf_saved;
    ok &= s3_rw(S3_LEDC_INT_ENA, 0x00080000, 0x00080000);
    *s3_reg(S3_LEDC_INT_ENA) = int_ena_saved;

    /* A duty update: DUTY_START, then PARA_UP; both bits self-clear. */
    *s3_reg(S3_LEDC_CH_DUTY(7)) = duty;
    *s3_reg(S3_LEDC_CH_CONF1(7)) = conf1 | S3_LEDC_CH_DUTY_START;
    ok &= s3_rw(S3_LEDC_CH_CONF0(7), conf0 | S3_LEDC_CH_PARA_UP, conf0);
    uint32_t conf1_after = *s3_reg(S3_LEDC_CH_CONF1(7));
    uint32_t duty_r = *s3_reg(S3_LEDC_CH_DUTY_R(7));

    ok = ok && conf1_after == conf1 && duty_r == duty;
    printf("LEDC_TEST regs: readback/self-clear %s (conf1=0x%08" PRIx32
           " duty_r=0x%08" PRIx32 ")\n", ok ? "OK" : "FAIL", conf1_after,
           duty_r);
    check(ok);
}

void app_main(void)
{
    printf("LEDC_TEST START\n");

    esp_err_t err = setup_timer(LEDC_TIMER_0, LEDC_TIMER_13_BIT);
    if (err == ESP_OK) {
        err = setup_timer(LEDC_TIMER_1, LEDC_TIMER_10_BIT);
    }
    if (err == ESP_OK) {
        err = setup_channel(LEDC_CHANNEL_0, LEDC_TIMER_0, CH0_GPIO);
    }
    if (err == ESP_OK) {
        err = setup_channel(LEDC_CHANNEL_1, LEDC_TIMER_1, CH1_GPIO);
    }
    printf("LEDC_TEST setup %s\n", esp_err_to_name(err));
    check(err == ESP_OK);

    if (err == ESP_OK) {
        test_duty(LEDC_CHANNEL_0, CH0_DUTY);
        test_duty(LEDC_CHANNEL_1, CH1_DUTY);
        test_fade();
    }
    test_registers();
    if (err == ESP_OK) {
        test_freq();
    }

    printf("LEDC_TEST RESULT %s\n", s_pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
