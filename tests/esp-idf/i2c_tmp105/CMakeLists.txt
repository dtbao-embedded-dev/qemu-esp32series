# ESP-IDF test app for the QEMU ESP32 / ESP32-S3 I2C controller models.
# Expects the default tmp105 slave at 0x48 on I2C0 and no slave on I2C1.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(i2c_tmp105)
