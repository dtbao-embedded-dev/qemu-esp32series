/*
 * WiFi SoftAP test for the QEMU ESP32-S3 machine.
 *
 * The firmware runs an open SoftAP "QEMU_S3_AP" (192.168.4.1). The QEMU
 * access point emulation joins it as station 02:53:36:30:00:02 and carries
 * the user-network host's traffic. Steps: start the AP -> a station
 * connects -> serve one HTTP request on port 80 coming from the host
 * through that station. Every result line starts with "SOFTAP_TEST"; the
 * last one is "SOFTAP_TEST RESULT PASS" or "SOFTAP_TEST RESULT FAIL".
 *
 * Run QEMU with
 *   -nic user,model=esp32s3_wifi,net=192.168.4.0/24,host=192.168.4.2,
 *        hostfwd=tcp:127.0.0.1:8081-192.168.4.1:80
 * and fetch http://127.0.0.1:8081/ once "http ready" shows.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define AP_SSID             "QEMU_S3_AP"
#define AP_CHANNEL          6
#define HTTP_BODY           "SOFTAP_S3_OK\n"
#define START_TIMEOUT_MS    5000
#define STA_TIMEOUT_MS      15000
#define HTTP_TIMEOUT_MS     60000

#define BIT_AP_STARTED      BIT0
#define BIT_STA_JOINED      BIT1
#define BIT_HTTP_SERVED     BIT2

static EventGroupHandle_t s_events;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        xEventGroupSetBits(s_events, BIT_AP_STARTED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *ev = data;

        printf("SOFTAP_TEST event: station " MACSTR " joined, aid=%d\n",
               MAC2STR(ev->mac), ev->aid);
        xEventGroupSetBits(s_events, BIT_STA_JOINED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        printf("SOFTAP_TEST event: station left\n");
    } else if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *ev = data;

        printf("SOFTAP_TEST event: station got " IPSTR "\n", IP2STR(&ev->ip));
    }
}

static bool wait_bit(EventBits_t bit, uint32_t timeout_ms)
{
    return xEventGroupWaitBits(s_events, bit, pdFALSE, pdTRUE,
                               pdMS_TO_TICKS(timeout_ms)) & bit;
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_sendstr(req, HTTP_BODY);
    xEventGroupSetBits(s_events, BIT_HTTP_SERVED);
    return ESP_OK;
}

static bool step_start_ap(void)
{
    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t cfg = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 4,
        },
    };
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
        esp_netif_create_default_wifi_ap();
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, on_event, NULL);
    }
    if (err == ESP_OK) {
        err = esp_wifi_init(&init_cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_AP);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    bool ok = err == ESP_OK && wait_bit(BIT_AP_STARTED, START_TIMEOUT_MS);
    printf("SOFTAP_TEST ap start %s\n",
           ok ? "OK" : (err != ESP_OK ? esp_err_to_name(err) : "TIMEOUT"));
    return ok;
}

static bool step_station(void)
{
    bool ok = wait_bit(BIT_STA_JOINED, STA_TIMEOUT_MS);

    printf("SOFTAP_TEST station joined %s\n", ok ? "OK" : "TIMEOUT");
    return ok;
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
        printf("SOFTAP_TEST http %s\n", esp_err_to_name(err));
        return false;
    }
    printf("SOFTAP_TEST http ready\n");
    bool ok = wait_bit(BIT_HTTP_SERVED, HTTP_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(500));     /* let the response go out */
    httpd_stop(server);
    printf("SOFTAP_TEST http served %s\n", ok ? "OK" : "TIMEOUT");
    return ok;
}

void app_main(void)
{
    bool pass;

    s_events = xEventGroupCreate();
    printf("SOFTAP_TEST START\n");
    pass = step_start_ap() && step_station() && step_http();
    printf("SOFTAP_TEST RESULT %s\n", pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
