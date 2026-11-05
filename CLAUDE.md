# qemu-esp32series

Fork of [espressif/qemu](https://github.com/espressif/qemu) (GPL-2.0),
imported from tag `esp-develop-9.2.2-20260417`. Remotes: `origin` is
`dtbao-embedded-dev/qemu-esp32series`, `upstream` is `espressif/qemu`.
Goal of the fork: extend the ESP32-S3 machine with peripherals upstream
does not model yet.

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
  source (0). The two NICs are exclusive: requesting both exits with an
  error.

## WiFi on the S3 machine

- `-nic user,model=esp32s3_wifi` creates the WiFi MAC
  (`hw/misc/esp32s3_wifi.c`, 0x60032000-0x60035fff) and the PHY/RF register
  stubs (`hw/misc/esp32s3_wifi_phy.c`). Without that NIC the machine is
  unchanged and `open_eth` takes a plain `-nic user`.
- The access point emulation (`hw/misc/esp32_wifi_ap.c`,
  `esp32_wlan_packet.c`, `esp32_wlan.h`, `include/hw/misc/esp32_wifi.h`) is
  taken from lcgamboa/qemu, branch `picsimlab-esp32`, commit 64aed503.
  It announces three open SSIDs: `PICSimLabWifi` (ch 1), `Espressif`
  (ch 5) and `MasseyWifi` (ch 10). There is no WPA2; connect with an empty
  password. Traffic goes through slirp (station gets 10.0.2.15).
- SoftAP: when the firmware beacons, the emulation joins it as station
  02:53:36:30:00:02 and carries the slirp host's traffic. Put slirp on the
  SoftAP subnet to reach it, e.g.
  `-nic user,model=esp32s3_wifi,net=192.168.4.0/24,host=192.168.4.2,hostfwd=tcp:127.0.0.1:8081-192.168.4.1:80`.
  The emulated station runs no DHCP client, so `IP_EVENT_AP_STAIPASSIGNED`
  never fires.
- The PHY stubs and the MAC layout come from `docs/wifi-s3-spike.md`. They
  are verified with ESP-IDF v6.1 only; the closed WiFi/PHY libraries change
  per release and may poll other registers.
- With the WiFi NIC, a blank eFuse MAC reads as the NIC MAC
  (`-nic ...,mac=`, default 52:54:00:12:34:56); only the in-memory eFuse is
  patched, never the image file, and a burned MAC wins.
- Original-ESP32 models are the usual porting base, but check the S3
  register layout in ESP-IDF (`components/soc/esp32s3`,
  `components/hal/esp32s3`) first; offsets and opcodes often differ.

## ADC on the S3 machine

- `hw/misc/esp32s3_sens.c` models the RTC side of SAR ADC1/ADC2 used by
  `adc_oneshot_*`, plus the temperature sensor (about 25 C). A conversion
  returns the value set for the selected channel (12 bits, default 0):
  `-global driver=esp32s3.sens,property=adc1-ch3,value=2048` (the short
  `-global esp32s3.sens.adc1-ch3=...` form does not work because the type
  name has a dot), or at run time `qom-set /machine/soc/sens adc1-ch3 2048`
  from the monitor.
- Not modelled: ADC continuous mode (`APB_SARADC` + GDMA), eFuse
  calibration (`adc_cali_*` returns "eFuse bits not burnt"), touch, ULP.

## GPIO on the S3 machine

- `hw/gpio/esp32s3_gpio.c` is a standalone model (no longer a child of the
  ESP32 `esp32.gpio`). It keeps OUT/ENABLE (+W1TS/W1TC) for GPIO0-48 and
  returns the pad levels in IN/IN1: a pad with its output enabled reads its
  own output, any other pad reads the level driven from outside.
- Drive inputs with the `in` property (uint64, bit n = GPIOn):
  `-global driver=esp32s3.gpio,property=in,value=<decimal mask>` at boot, or
  `qom-set /machine/soc/gpio in <mask>` from the monitor. Other QEMU devices
  can drive the 49 qdev input lines named `pin`.
- Interrupts: GPIO_PINn.INT_TYPE (pos/neg/any edge, low/high level) sets
  STATUS; pins with the CPU bit of INT_ENA show in PCPU_INT/PCPU_INT1 (what
  the IDF ISR reads) and raise `ETS_GPIO_INTR_SOURCE`. A level type sets its
  STATUS bit again after a W1TC while the level holds. The NMI and SDIO
  sources are not wired.
- Not modelled: IO_MUX and the GPIO matrix routing (FUNCn_IN_SEL/OUT_SEL only
  keep their value), pull-ups/downs, open drain, RTC IO.

## GP-SPI2/SPI3 on the S3 machine

- `hw/ssi/esp32s3_gpspi.c` models the GP-SPI master user transaction
  (command, address, dummy, MOSI/MISO data, full or half duplex) with data in
  W0..W15; `SPI_CMD.USR` and `UPDATE` complete at once and set `trans_done`
  in `SPI_DMA_INT_RAW`, which drives `ETS_SPI2/3_INTR_SOURCE`.
- DMA mode (`SPI_DMA_CH_AUTO`): with `DMA_TX_ENA`/`DMA_RX_ENA` set, the data
  phase reads from / writes to the GDMA OUT/IN channel whose PERI_SEL is the
  SPI's GDMA ID (SPI2 = 0, SPI3 = 1) and whose link was started; transfers
  are not limited to 64 bytes.
- The SSI buses are named `spi2` and `spi3`; attach QEMU SSI devices with
  `-device w25x16,bus=spi2,cs=0` (`cs` = the CSn line, i.e. the slot the IDF
  driver gives the device: the first `spi_bus_add_device` is CS0). A device
  without a drive gets blank (0xFF) storage.
- Every phase moves whole bytes on one data line, except the dummy phase:
  one transfer per dummy cycle, the convention the m25p80 flash model uses.
  Not modelled: dual/quad/octal lines, CS polarity, clock timing, slave mode.
- The interrupt matrix (`hw/xtensa/esp32s3_intc.c`) re-applies a source's
  current level when its routing changes: ESP-IDF disables a non-shared
  interrupt by routing it to a reserved CPU interrupt and back, and the SPI
  driver relies on `trans_done` staying high while it is disabled.

## Build and run

Verified on this Windows machine with MSYS2 MINGW64, out of tree in
`build-win/` (ignored locally through `.git/info/exclude`):

```sh
mkdir build-win && cd build-win
../configure --target-list=xtensa-softmmu --enable-gcrypt --enable-pixman \
  --enable-slirp --enable-stack-protector --static \
  --without-default-features --extra-cflags=-Werror
ninja qemu-system-xtensa.exe
```

- The feature flags match `.github/workflows/scripts/configure-win.sh`.
  Slirp is mandatory: `meson.build` wraps it in `declare_dependency`, so
  `CONFIG_SLIRP` is set even when slirp is disabled, and the fork links it
  statically, which needs `--static`. After configure, apply the same
  `sed` fix for `libintl`/`libiconv` that the CI script applies to
  `build.ninja`.
- Without Windows Developer Mode, the meson postconf step
  `scripts/symlink-install-tree.py` fails because it cannot create
  symlinks. It ignores `EEXIST`, so pre-create the `qemu-bundle/` entries
  listed by `meson introspect --installed` as plain files and rerun
  configure. An out-of-tree build keeps `qemu-bundle/` between runs.

Run an ESP-IDF image:

```sh
build-win/qemu-system-xtensa.exe -M esp32s3 -display none -serial stdio \
  -monitor none -no-reboot -L pc-bios -drive file=flash.bin,if=mtd,format=raw
```

Test firmware: `tests/esp-idf/i2c_tmp105` (both `esp32` and `esp32s3`),
passes with ESP-IDF 6.0.2 and 6.1. Merge the image with
`esptool --chip <target> merge_bin --fill-flash-size 4MB -o flash.bin @flash_args`
from the IDF build directory. A passing run prints `I2C_TEST RESULT PASS`.
`tests/esp-idf/ledc_s3` (`esp32s3` only, the ESP32 LEDC model has no fade
or `DUTY_R`) prints `LEDC_TEST RESULT PASS`; add
`-trace led_set_intensity` to see the LEDC channel outputs.
`tests/esp-idf/wifi_s3` needs
`-nic user,model=esp32s3_wifi,hostfwd=tcp:127.0.0.1:8080-:80` and
`-global driver=esp32s3.sens,property=adc2-ch1,value=1234` (it reads ADC2
while WiFi runs); it scans,
connects, gets an IP, echoes TCP and UDP with
`tests/esp-idf/wifi_s3/host_echo.py` (run it on the host first; the guest
reaches it as 10.0.2.2:5555), then waits up to 60 s for one request to
`http://127.0.0.1:8080/` from the host before printing
`WIFI_TEST RESULT PASS`. `tests/esp-idf/adc_s3` needs the four `-global driver=esp32s3.sens,...`
values listed at the top of `main/adc_s3_test.c` and a monitor
(`-monitor tcp:127.0.0.1:4444,server,nowait`) to send the `qom-set` it
waits for after `ADC_TEST ready`; it prints `ADC_TEST RESULT PASS`.
`tests/esp-idf/softap_s3` needs the SoftAP `-nic` line above; fetch
`http://127.0.0.1:8081/` after `SOFTAP_TEST http ready`, then it prints
`SOFTAP_TEST RESULT PASS`.
`tests/esp-idf/gpio_s3` needs
`-global driver=esp32s3.gpio,property=in,value=2199023255584` and a monitor
to send `qom-set /machine/soc/gpio in 4398046511168` after `GPIO_TEST ready`
and `qom-set /machine/soc/gpio in 4398046513216` after `GPIO_TEST irq ready`;
it prints `GPIO_TEST RESULT PASS`.
`tests/esp-idf/spi_s3` needs
`-device w25x16,bus=spi2,cs=0 -device w25x16,bus=spi3,cs=1` and prints
`SPI_TEST RESULT PASS`.
`tests/esp-idf/openeth_s3` needs
`-nic user` (or `model=open_eth`) and prints `OPENETH_TEST RESULT PASS`
once DHCP succeeds.

ESP-IDF v6.1 is the release that pins this fork's QEMU
(`esp-develop-9.2.2-20260417` in `tools/tools.json`). Local installs live
under `C:\Espressif\frameworks` (v4.4.6 to v6.0.2) and `E:\.espressif`
(v5.3.5, v5.5.4, v6.0.1, v6.1); the tools in `E:\.espressif` cover 6.0.2
and 6.1. `export.ps1` fails on the missing pinned openocd, so set
`IDF_PATH`, `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH` and `PATH` by hand.

## Rules

- Files, code comments and commit messages are in English.
- Keep upstream code style (QEMU `scripts/checkpatch.pl`).
- Changes to shared ESP32 models must not change the `-M esp32` behaviour.
