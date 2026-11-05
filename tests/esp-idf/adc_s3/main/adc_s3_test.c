/*
 * RTC ADC and temperature sensor test for the QEMU ESP32-S3 machine.
 *
 * QEMU must be started with these channel values:
 *   -global driver=esp32s3.sens,property=adc1-ch0,value=1000
 *   -global driver=esp32s3.sens,property=adc1-ch3,value=2048
 *   -global driver=esp32s3.sens,property=adc1-ch6,value=4095
 *   -global driver=esp32s3.sens,property=adc2-ch1,value=1234
 * After "ADC_TEST ready" the runner sets adc1-ch3 to 3000 from the monitor:
 *   qom-set /machine/soc/sens adc1-ch3 3000
 * Every result line starts with "ADC_TEST"; the last one is
 * "ADC_TEST RESULT PASS" or "ADC_TEST RESULT FAIL".
 */

#include <stdbool.h>
#include <stdio.h>

#include "driver/temperature_sensor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define QOM_SET_VALUE       3000
#define QOM_SET_TIMEOUT_MS  20000

static const struct {
    adc_unit_t unit;
    adc_channel_t channel;
    int expected;
} s_reads[] = {
    { ADC_UNIT_1, ADC_CHANNEL_0, 1000 },
    { ADC_UNIT_1, ADC_CHANNEL_3, 2048 },
    { ADC_UNIT_1, ADC_CHANNEL_6, 4095 },
    { ADC_UNIT_2, ADC_CHANNEL_1, 1234 },
};

static bool s_pass = true;

static void check(bool ok)
{
    if (!ok) {
        s_pass = false;
    }
}

static adc_oneshot_unit_handle_t new_unit(adc_unit_t unit)
{
    const adc_oneshot_unit_init_cfg_t cfg = { .unit_id = unit };
    adc_oneshot_unit_handle_t handle = NULL;
    esp_err_t err = adc_oneshot_new_unit(&cfg, &handle);

    printf("ADC_TEST unit %d init %s\n", unit + 1, esp_err_to_name(err));
    check(err == ESP_OK);
    return err == ESP_OK ? handle : NULL;
}

static esp_err_t read_channel(adc_oneshot_unit_handle_t handle,
                              adc_channel_t channel, int *raw)
{
    const adc_oneshot_chan_cfg_t cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_oneshot_config_channel(handle, channel, &cfg);

    return err == ESP_OK ? adc_oneshot_read(handle, channel, raw) : err;
}

static void test_temperature(void)
{
    const temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    temperature_sensor_handle_t tsens = NULL;
    float celsius = 0;
    esp_err_t err = temperature_sensor_install(&cfg, &tsens);

    if (err == ESP_OK) {
        err = temperature_sensor_enable(tsens);
    }
    if (err == ESP_OK) {
        err = temperature_sensor_get_celsius(tsens, &celsius);
    }
    bool ok = err == ESP_OK && celsius > 15 && celsius < 35;
    printf("ADC_TEST tsens %.1f C %s\n", celsius, ok ? "OK" : esp_err_to_name(err));
    check(ok);
}

void app_main(void)
{
    adc_oneshot_unit_handle_t units[2];

    printf("ADC_TEST START\n");
    units[0] = new_unit(ADC_UNIT_1);
    units[1] = new_unit(ADC_UNIT_2);

    for (size_t i = 0; i < sizeof(s_reads) / sizeof(s_reads[0]); i++) {
        adc_oneshot_unit_handle_t h = units[s_reads[i].unit];
        int raw = -1;
        esp_err_t err = h ? read_channel(h, s_reads[i].channel, &raw) : ESP_FAIL;
        bool ok = err == ESP_OK && raw == s_reads[i].expected;

        printf("ADC_TEST adc%d ch%d raw=%d %s\n", s_reads[i].unit + 1,
               s_reads[i].channel, raw, ok ? "OK" : esp_err_to_name(err == ESP_OK ? ESP_FAIL : err));
        check(ok);
    }

    /* Value changed from the QEMU monitor while the app runs */
    if (units[0]) {
        int raw = -1;
        printf("ADC_TEST ready\n");
        for (int t = 0; t < QOM_SET_TIMEOUT_MS / 100 && raw != QOM_SET_VALUE; t++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            read_channel(units[0], ADC_CHANNEL_3, &raw);
        }
        printf("ADC_TEST qom-set adc1 ch3 raw=%d %s\n", raw,
               raw == QOM_SET_VALUE ? "OK" : "TIMEOUT");
        check(raw == QOM_SET_VALUE);
    }

    test_temperature();

    printf("ADC_TEST RESULT %s\n", s_pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
