/*
 * OpenCores Ethernet test for the QEMU ESP32-S3 machine.
 *
 * Brings up the open_eth NIC with DHCP and waits for an address from slirp.
 * Prints "OPENETH_TEST got ip <addr> OK" and a final
 * "OPENETH_TEST RESULT PASS" or "OPENETH_TEST RESULT FAIL".
 */

#include <stdbool.h>
#include <stdio.h>

#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define IP_TIMEOUT_MS   20000

static SemaphoreHandle_t s_got_ip;
static esp_ip4_addr_t s_ip;

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *ev = data;

    s_ip = ev->ip_info.ip;
    xSemaphoreGive(s_got_ip);
}

static esp_err_t eth_start(void)
{
    const esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    esp_eth_handle_t eth = NULL;
    esp_netif_t *netif;
    esp_err_t err;

    phy_cfg.autonego_timeout_ms = 100;
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_cfg);
    if (mac == NULL || phy == NULL) {
        return ESP_FAIL;
    }
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);

    err = esp_eth_driver_install(&eth_cfg, &eth);
    if (err != ESP_OK) {
        return err;
    }
    netif = esp_netif_new(&netif_cfg);
    err = esp_netif_attach(netif, esp_eth_new_netif_glue(eth));
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, NULL);
    }
    if (err == ESP_OK) {
        err = esp_eth_start(eth);
    }
    return err;
}

void app_main(void)
{
    esp_err_t err;
    bool pass = false;

    s_got_ip = xSemaphoreCreateBinary();
    printf("OPENETH_TEST START\n");
    err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
    }
    if (err == ESP_OK) {
        err = eth_start();
    }
    printf("OPENETH_TEST start %s\n", esp_err_to_name(err));
    if (err == ESP_OK) {
        pass = xSemaphoreTake(s_got_ip, pdMS_TO_TICKS(IP_TIMEOUT_MS));
        if (pass) {
            printf("OPENETH_TEST got ip " IPSTR " OK\n", IP2STR(&s_ip));
        } else {
            printf("OPENETH_TEST got ip TIMEOUT\n");
        }
    }
    printf("OPENETH_TEST RESULT %s\n", pass ? "PASS" : "FAIL");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
