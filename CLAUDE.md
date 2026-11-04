# qemu-esp32series

Fork of [espressif/qemu](https://github.com/espressif/qemu) (GPL-2.0),
imported from tag `esp-develop-9.2.2-20260417`. Remotes: `origin` is
`dtbao-embedded-dev/qemu-esp32series`, `upstream` is `espressif/qemu`.
Goal of the fork: extend the ESP32-S3 machine with peripherals upstream
does not model yet.

## Active work

- [docs/plan-esp32s3-i2c-ledc-wifi.md](docs/plan-esp32s3-i2c-ledc-wifi.md):
  plan and progress for I2C0/1, LEDC and WiFi on ESP32-S3. Update its
  Progress table with each step; delete the file (and this link) when all
  steps are done.

## Layout (ESP32 parts)

| Machine | Board / SoC | Interrupts | Clock |
|---|---|---|---|
| `-M esp32` | `hw/xtensa/esp32.c` | `hw/xtensa/esp32_intc.c` | in `hw/misc/` |
| `-M esp32s3` | `hw/xtensa/esp32s3.c` | `hw/xtensa/esp32s3_intc.c` | `hw/xtensa/esp32s3_clk.c` |
| `-M esp32c3` | `hw/riscv/esp32c3.c` | `hw/riscv/esp32c3_intmatrix.c` | `hw/riscv/esp32c3_clk.c` |

- S3 register map: `include/hw/misc/esp32s3_reg.h`.
  S3 interrupt sources: `include/hw/xtensa/esp32s3_intc.h`.
- Peripheral models: `hw/misc/esp32*`, `hw/i2c/esp32_i2c.c`,
  `hw/ssi/`, `hw/char/`, `hw/net/opencores_eth.c`.
- Kconfig: `hw/xtensa/Kconfig` (`XTENSA_ESP32`, `XTENSA_ESP32S3`).
  Sources are added per machine in each directory's `meson.build`.

## Conventions on the S3 machine

- Peripherals with no model fall into the catch-all `esp32s3.iomem`
  region (0x60000000, size 0xd1000): reads return 0, writes are ignored.
  New devices are mapped above it with
  `memory_region_add_subregion_overlap(sys_mem, BASE, mr, 0)`.
- Device wiring pattern in `esp32s3.c`: `object_initialize_child` in init,
  `sysbus_realize`, map MMIO, `sysbus_connect_irq` to
  `qdev_get_gpio_in(DEVICE(&ss->intmatrix), ETS_*_INTR_SOURCE)`, and add
  the device to `esp32s3_soc_reset`.
- `open_eth` uses `ETS_ETH_MAC_INTR_SOURCE`, which aliases the WiFi MAC
  source (0).
- Original-ESP32 models are the usual porting base, but check the S3
  register layout in ESP-IDF (`components/soc/esp32s3`,
  `components/hal/esp32s3`) first; offsets and opcodes often differ.

## Build and run

Not yet verified in this checkout (no `build/` directory exists). Expected:

```sh
./configure --target-list=xtensa-softmmu,riscv32-softmmu
ninja -C build
build/qemu-system-xtensa -M esp32s3 -nographic \
  -drive file=flash.bin,if=mtd,format=raw
```

ESP-IDF test firmware: local installs under `C:\Espressif\frameworks`
(v4.4.6, v5.1.6, v5.3.3, v5.5.3, v6.0.2).

## Rules

- Files, code comments and commit messages are in English.
- Keep upstream code style (QEMU `scripts/checkpatch.pl`).
- Changes to shared ESP32 models must not change the `-M esp32` behaviour.
