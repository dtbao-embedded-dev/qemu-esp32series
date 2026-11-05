/*
 * WiFi station test for the QEMU ESP32-S3 machine.
 *
 * Steps: init -> start -> scan -> connect to the open AP "Espressif" ->
 * get an IP -> serve one HTTP request on port 80. Each step has a timeout,
 * so a missing piece of the emulation shows up as a FAIL line instead of
 * a hang. Every result line starts with "WIFI_TEST"; the last one is
 * "WIFI_TEST RESULT PASS" or "WIFI_TEST RESULT FAIL".
 *
 * Run QEMU with -nic user,model=esp32s3_wifi,hostfwd=tcp:127.0.0.1:8080-:80
 * and fetch http://127.0.0.1:8080/ from the host once "http ready" shows.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define TEST_SSID           "Espressif"
#define HTTP_BODY           "WIFI_S3_OK\n"
#define START_TIMEOUT_MS    5000
#define SCAN_TIMEOUT_MS     15000
#define CONNECT_TIMEOUT_MS  15000
#define IP_TIMEOUT_MS       20000
#define HTTP_TIMEOUT_MS     60000
#define MAX_APS             16

#define BIT_STARTED         BIT0
#define BIT_SCAN_DONE       BIT1
#define BIT_CONNECTED       BIT2
#define BIT_GOT_IP          BIT3
#define BIT_HTTP_SERVED     BIT4

static EventGroupHandle_t s_events;
static esp_ip4_addr_t s_ip;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            xEventGroupSetBits(s_events, BIT_STARTED);
            break;
        case WIFI_EVENT_SCAN_DONE:
            xEventGroupSetBits(s_events, BIT_SCAN_DONE);
            break;
        case WIFI_EVENT_STA_CONNECTED:
            xEventGroupSetBits(s_events, BIT_CONNECTED);
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *d = data;
            printf("WIFI_TEST event: disconnected reason=%d\n", d->reason);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;

        s_ip = ev->ip_info.ip;
        xEventGroupSetBits(s_events, BIT_GOT_IP);
    }
}

static bool wait_bit(EventBits_t bit, uint32_t timeout_ms)
{
    return xEventGroupWaitBits(s_events, bit, pdFALSE, pdTRUE,
                               pdMS_TO_TICKS(timeout_ms)) & bit;
}

static bool report(const char *step, esp_err_t err, bool ok)
{
    printf("WIFI_TEST %s %s\n", step,
           ok ? "OK" : (err != ESP_OK ? esp_err_to_name(err) : "TIMEOUT"));
    return ok;
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_sendstr(req, HTTP_BODY);
    xEventGroupSetBits(s_events, BIT_HTTP_SERVED);
    return ESP_OK;
}

static bool step_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err == ESP_OK) {
        err = esp_netif_init();
    }
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
    }
    if (err == ESP_OK) {
        esp_netif_create_default_wifi_sta();
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    }
    if (err == ESP_OK) {
        const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

        printf("WIFI_TEST init: calling esp_wifi_init\n");
        err = esp_wifi_init(&cfg);
    }
    return report("init", err, err == ESP_OK);
}

static bool step_start(void)
{
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (err == ESP_OK) {
        printf("WIFI_TEST start: calling esp_wifi_start\n");
        err = esp_wifi_start();
    }
    return report("start", err, err == ESP_OK && wait_bit(BIT_STARTED, START_TIMEOUT_MS));
}

/* The access points the QEMU emulation announces, with their channels */
static const struct {
    const char *ssid;
    uint8_t channel;
} s_expected_aps[] = {
    { "PICSimLabWifi", 1 },
    { TEST_SSID, 5 },
    { "MasseyWifi", 10 },
};

static bool step_scan(void)
{
    static wifi_ap_record_t aps[MAX_APS];
    uint16_t count = MAX_APS;
    size_t found = 0;
    esp_err_t err = esp_wifi_scan_start(NULL, false);

    if (err != ESP_OK || !wait_bit(BIT_SCAN_DONE, SCAN_TIMEOUT_MS)) {
        return report("scan", err, false);
    }
    err = esp_wifi_scan_get_ap_records(&count, aps);
    for (uint16_t i = 0; err == ESP_OK && i < count; i++) {
        printf("WIFI_TEST scan ap ssid=%s ch=%d rssi=%d auth=%d\n",
               (const char *)aps[i].ssid, aps[i].primary, aps[i].rssi,
               aps[i].authmode);
        for (size_t j = 0; j < sizeof(s_expected_aps) / sizeof(s_expected_aps[0]); j++) {
            if (strcmp((const char *)aps[i].ssid, s_expected_aps[j].ssid) == 0 &&
                aps[i].primary == s_expected_aps[j].channel) {
                found++;
            }
        }
    }
    bool ok = err == ESP_OK &&
              found == sizeof(s_expected_aps) / sizeof(s_expected_aps[0]);
    printf("WIFI_TEST scan count=%u expected found=%u %s\n", count,
           (unsigned)found, ok ? "OK" : (err != ESP_OK ? esp_err_to_name(err) : "NOT_FOUND"));
    return ok;
}

static bool step_connect(void)
{
    wifi_config_t cfg = { 0 };
    esp_err_t err;

    strcpy((char *)cfg.sta.ssid, TEST_SSID);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    if (!report("connect", err, err == ESP_OK &&
                wait_bit(BIT_CONNECTED, CONNECT_TIMEOUT_MS))) {
        return false;
    }
    if (!wait_bit(BIT_GOT_IP, IP_TIMEOUT_MS)) {
        return report("got ip", ESP_OK, false);
    }
    printf("WIFI_TEST got ip " IPSTR " OK\n", IP2STR(&s_ip));
    return true;
}

static bool step_http(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get,
    };
    esp_err_t err = httpd_start(&server, &cfg);

    if (err == ESP_OK) {
        err = httpd_register_uri_handler(server, &root);
    }
    if (err != ESP_OK) {
        return report("http", err, false);
    }
    printf("WIFI_TEST http ready\n");
    bool ok = wait_bit(BIT_HTTP_SERVED, HTTP_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(500));     /* let the response go out */
    httpd_stop(server);
    return report("http served", ESP_OK, ok);
}

void app_main(void)
{
    bool pass;

    s_events = xEventGroupCreate();
    printf("WIFI_TEST START\n");
    pass = step_init() && step_start() && step_scan() && step_connect() &&
           step_http();
    printf("WIFI_TEST RESULT %s\n", pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
