# ESP-IDF test app for the QEMU ESP32-S3 GPIO model.
# Run QEMU with the input levels the app expects, for example
#   -global driver=esp32s3.gpio,property=in,value=2199023255584
# (see main/gpio_s3_test.c for the full list of steps).
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gpio_s3)
