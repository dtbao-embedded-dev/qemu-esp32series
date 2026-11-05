# ESP-IDF SoftAP test app for the QEMU ESP32-S3 WiFi emulation.
# Run QEMU with
#   -nic user,model=esp32s3_wifi,net=192.168.4.0/24,host=192.168.4.2,
#        hostfwd=tcp:127.0.0.1:8081-192.168.4.1:80
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(softap_s3)
