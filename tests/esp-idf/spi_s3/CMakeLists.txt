# ESP-IDF test app for the QEMU ESP32-S3 GP-SPI (SPI2/SPI3) master model.
# Run QEMU with an SPI flash on each bus:
#   -device w25x16,bus=spi2,cs=0 -device w25x16,bus=spi3,cs=1
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(spi_s3)
