idf_component_register(SRCS "softap_s3_test.c"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash
                                     esp_http_server)
