# ESP-IDF test app for the QEMU ESP32-S3 WiFi emulation.
# Run QEMU with -nic user,model=esp32s3_wifi,hostfwd=tcp:127.0.0.1:8080-:80
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wifi_s3)
