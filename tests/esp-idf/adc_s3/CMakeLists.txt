# ESP-IDF test app for the QEMU ESP32-S3 RTC ADC (SENS) model.
# Run QEMU with the channel values the app expects, for example
#   -global driver=esp32s3.sens,property=adc1-ch0,value=1000
# (see main/adc_s3_test.c for the full list).
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(adc_s3)
