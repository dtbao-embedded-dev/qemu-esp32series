# Plan: ESP32-S3 ADC oneshot and WiFi completion

Working plan for the `-M esp32s3` machine after the I2C, LEDC and WiFi work.
Phase A finishes the WiFi emulation; phase B adds the RTC ADC used by
`adc_oneshot_*`. Reference firmware: ESP-IDF v6.1.

> **Lifecycle:** this is a working plan, not documentation. Update the
> Progress section as each step lands. When every step is `done` (or
> explicitly `dropped`), delete this file and its link in `CLAUDE.md`
> ("Active work") in the same commit that finishes the last step.

## Progress

Status values: `todo`, `wip`, `done`, `blocked`, `dropped`.

| Step | What | Status | Notes |
|---|---|---|---|
| W4 | Outbound TCP and UDP from the firmware to the host | todo | |
| W5 | Non-zero station MAC when the eFuse MAC is blank | todo | |
| A1 | `esp32s3.sens` model: RTC ADC oneshot + TSENS | todo | |
| A2 | Per-channel input values (properties, `qom-set`) | todo | |
| A4 | `tests/esp-idf/adc_s3` + IDF `adc/oneshot_read` | todo | |
| A3 | ADC2 while WiFi is on (arbiter flags) | todo | |
| W6 | SoftAP: emulated station joins and gets an IP | todo | |

Order is W4, W5, A1, A2, A4, A3, W6: W6 relies on the SoftAP part of the
lcgamboa access point, which has not been run on the S3 yet.

### Log

Append one line per working session: date, step IDs touched, outcome.

- 2026-11-05: plan written. No code changed yet.

## Constraints the request did not mention

1. **TSENS lives in the WiFi PHY stub today.** `esp32s3.wifi_phy` maps
   `SENS_SAR_TSENS_CTRL_REG` (0x60008850) when the WiFi NIC is present. The
   SENS model must take that register over, and `esp_wifi_start()` must
   still get through PHY calibration.
2. **ADC2 is shared with WiFi through an arbiter.** With WiFi on, a raw
   ADC2 value without the arbiter's valid flags makes `adc_oneshot_read()`
   fail (`adc_oneshot_ll_raw_check_valid`).
3. **ADC driver init calibrates through regi2c (0x6000e000).** Without the
   WiFi NIC that range is the catch-all region; A1 starts with a short check
   for wait loops there.

## Phase A: WiFi completion

### W4. Outbound TCP and UDP

- `tests/esp-idf/wifi_s3`: after getting an IP, open a TCP connection and
  send a UDP datagram to the slirp host address 10.0.2.2, on ports a small
  host-side server listens on, and check the echoed data.
- Done when the firmware prints `tcp out OK` and `udp out OK` and the host
  server saw the same strings. DNS to the internet is not checked: it
  depends on the machine having network access.

### W5. Station MAC

- When the WiFi NIC is created and the eFuse MAC (BLOCK1 `MAC_SPI_SYS_0/1`)
  is blank, write the NIC MAC (`-nic ...,mac=`, default 52:54:00:12:34:56)
  into the in-memory eFuse so `esp_read_mac()` returns it. The eFuse image
  file is not modified.
- Done when the boot log shows `wifi:mode : sta (52:54:00:12:34:56)` and
  `esp_read_mac` matches; a machine without the WiFi NIC keeps a blank MAC.

### W6. SoftAP

- Firmware in AP mode; the lcgamboa emulation joins as station
  02:53:36:30:00:02.
- Done when the firmware sees `WIFI_EVENT_AP_STACONNECTED` and
  `IP_EVENT_AP_STAIPASSIGNED`.

## Phase B: ADC oneshot

On the S3, `adc_oneshot_*` uses the RTC ADC in SENS (0x60008800), not
`APB_SARADC`.

### A1. `esp32s3.sens`

- `SAR_MEAS1_CTRL2` (0x0c) and `SAR_MEAS2_CTRL2` (0x30): a write with
  `MEAS*_START_SAR` (bit 17) sets `MEAS*_DONE_SAR` (bit 16) and puts the
  value of the channel selected in `SAR*_EN_PAD` (bits 19-30) in
  `MEAS*_DATA_SAR` (bits 0-15).
- `SAR_SLAVE_ADDR1` (0x40): `meas_status` reads 0.
- Other registers read back. `SAR_TSENS_CTRL` (0x50) moves here from the
  WiFi PHY stub (ready bit 8, raw value about 25 C).

### A2. Input values

- Properties `adc1-ch0` .. `adc1-ch9` and `adc2-ch0` .. `adc2-ch9`
  (0-4095), set with `-global esp32s3.sens.adc1-ch3=2048` or at run time
  with `qom-set`.

### A3. ADC2 and the arbiter

- With WiFi on, ADC2 raw values carry the flags the driver expects, so
  `adc_oneshot_read()` on ADC2 succeeds.

### A4. Verification

- `tests/esp-idf/adc_s3`: reads several ADC1 and ADC2 channels and compares
  them with the configured values; a run with `qom-set` changes a value
  while the app runs.
- IDF example `peripherals/adc/oneshot_read` prints the configured values.

### Out of scope

ADC continuous mode (`APB_SARADC` + GDMA), eFuse-based calibration (the
default eFuse image has no calibration data, so `adc_cali_*` returns
`ESP_ERR_NOT_SUPPORTED`), touch sensor, WPA2/EAPOL, ESP-NOW.

## Regression

Every step keeps these passing: `i2c_tmp105` (`-M esp32` and
`-M esp32s3`), `ledc_s3`, `wifi_s3` with
`-nic user,model=esp32s3_wifi`, `openeth_s3` with `-nic user`.
