# Plan: I2C0/1, LEDC and WiFi for the ESP32-S3 machine

> **Lifecycle:** this is a working plan, not documentation. Update the
> Progress section as each step lands. When every step is `done` (or
> explicitly `dropped`), delete this file and its link in `CLAUDE.md`
> ("Active work") in the same commit that finishes the last step. Sphinx
> only builds `.rst`, so this file never enters the generated docs.

## Progress

Status values: `todo`, `wip`, `done`, `blocked`, `dropped`.

| ID  | Step                                                          | Status | Commit / note |
|-----|---------------------------------------------------------------|--------|---------------|
| I1  | Parametrize `esp32_i2c.c` (opcode map, cmd count, MMIO size)  | done   | `-M esp32` test output identical before/after |
| I2  | Add `esp32s3.i2c` subtype with S3-only registers              | done   | `hw/i2c/esp32s3_i2c.c` |
| I3  | Instantiate I2C0/I2C1 in `esp32s3.c` (map, IRQ, reset)        | done   | `info mtree`: 0x60013000, 0x60027000 |
| I4  | Attach default slave `tmp105@0x48` on I2C0                    | done   |               |
| I5  | Verify with IDF test apps + ESP32 regression                  | done   | `i2c_tmp105` PASS on esp32s3 (IDF 6.0.2, 6.1); 5.5.3 not run |
| L1  | New model `hw/misc/esp32s3_ledc.c`                            | done   | duty, `DUTY_R`, LEDs; `ledc_s3` test |
| L2  | Instant fade + `DUTY_CHNG_END` interrupt                      | todo   |               |
| L3  | Wire LEDC into `esp32s3.c`, Kconfig `select LED`, meson       | done   | `info mtree`: 0x60019000 |
| L4  | Verify with `ledc_basic` / `ledc_fade`                        | todo   |               |
| W0  | Decide WiFi direction (see Phase 3)                           | todo   |               |
| W1  | Short-term networking: `open_eth` (B) or firmware shim (C)    | todo   |               |
| W2  | Spike (1-2 days): map lcgamboa `esp32c3_wifi` onto S3         | todo   |               |
| W3  | Port WiFi emulation to S3 (only if W2 says go)                | todo   |               |

### Log

Append one line per working session: date, step IDs touched, outcome.

- 2026-11-04: plan written. No code changed yet.
- 2026-11-04: I1 done. Added `tests/esp-idf/i2c_tmp105`, built with
  ESP-IDF 6.0.2 because 5.5.3 is not fully installed here. Baseline on
  `-M esp32` passes; `-M esp32s3` fails (no I2C yet).
- 2026-11-04: I2-I5 done. `-M esp32s3` passes the new driver, an empty
  I2C1 probe (NOT_FOUND), a 13-command legacy transaction and the
  register readback/self-clear checks. `-M esp32` output is unchanged.
  Not run: IDF 5.5.3 (Python env and esp-14.2.0 toolchain missing).
- 2026-11-04: I5 rerun with ESP-IDF v6.1 (pins this fork's QEMU,
  esp-develop-9.2.2-20260417): `i2c_tmp105` PASS on `-M esp32` and
  `-M esp32s3`, same output as with 6.0.2.
- 2026-11-04: L1, L3 done. New `esp32s3.ledc` model wired at 0x60019000
  (IRQ 35). `tests/esp-idf/ledc_s3` (IDF v6.1) passes: CH0 4096 and CH1
  256 read back, trace shows 50% and 25%, register readback and
  self-clear OK, `ledc_get_freq` 5000. `i2c_tmp105` still passes on both
  machines. The DUTY register has 4 fractional bits, not 8.

### Open decisions

1. WiFi direction: A, B, C, or B/C plus the A spike (recommended).
2. Extra I2C slaves besides `tmp105` (EEPROM, OLED, a specific sensor).
3. Reference ESP-IDF version for testing: 6.1 (pins this QEMU) or 6.0.2.
   Matters most for WiFi option A because the closed blobs change per
   release.

## Current state

- **I2C:** `hw/i2c/esp32_i2c.c` is already built for S3
  (`hw/i2c/meson.build:19`) but `hw/xtensa/esp32s3.c` creates no instance.
  `ESP32S3_I2C_COUNT 2` exists in `include/hw/misc/esp32s3_reg.h` and is
  unused.
- **LEDC:** only the original ESP32 model exists (`hw/misc/esp32_ledc.c`,
  ESP32-only in `hw/misc/meson.build`). `XTENSA_ESP32S3` in
  `hw/xtensa/Kconfig` does not `select LED`.
- **WiFi:** nothing in this fork. S3 only has `open_eth` at
  `DR_REG_EMAC_BASE` (0x600CD000).
- Addresses and interrupt sources already defined:
  `DR_REG_I2C_EXT_BASE` 0x60013000, `DR_REG_I2C1_EXT_BASE` 0x60027000,
  `DR_REG_LEDC_BASE` 0x60019000, `ETS_I2C_EXT0/1_INTR_SOURCE`,
  `ETS_LEDC_INTR_SOURCE` (35).
- Unmodelled peripherals fall into the `esp32s3.iomem` catch-all
  (0x60000000, size 0xd1000): reads return 0, writes are ignored.

## Constraints the request did not mention

1. **The ESP32 I2C model fails silently on S3.** S3 opcodes
   (`hal/esp32s3/include/hal/i2c_ll.h:45-49`) are RESTART=6, WRITE=1,
   READ=3, STOP=2, END=4; ESP32 uses RSTART=0, WRITE=1, READ=2, STOP=3,
   END=4. S3 has 8 COMD registers (0x58-0x74) instead of 16, self-clearing
   `CTR.CONF_UPGATE` (bit 11) and `CTR.FSM_RST` (bit 10), and FIFO windows
   at 0x100 (TX) and 0x180 (RX), so the MMIO region must be at least 0x200.
2. **S3 LEDC has a different register layout.** Low-speed only: 8 channels
   at `0x14*n`, 4 timers at `0xA0+8n`, interrupts at 0xC0, `LEDC_CONF` at
   0xD0. Only the duty-to-intensity math is reusable from the ESP32 model.
3. **`open_eth` already owns the WiFi MAC interrupt source.**
   `include/hw/xtensa/esp32s3_intc.h:150` defines
   `ETS_ETH_MAC_INTR_SOURCE` as `ETS_WIFI_MAC_INTR_SOURCE` (0). A WiFi
   model and `open_eth` cannot both use it; one must move or be disabled.

## Phase 1: I2C0/1 (about 2-3 days)

### I1. Parametrize the shared model

Move the opcode table, command register count and MMIO size into class
data of `TYPE_ESP32_I2C`. The ESP32 machine keeps its current values.

Verify: ESP32 machine builds and an existing ESP32 I2C firmware still reads
`tmp105` unchanged.

### I2. `esp32s3.i2c` subtype

- `CTR.CONF_UPGATE` and `CTR.FSM_RST` self-clear on write.
- `SCL_SP_CONF.scl_rst_slv_en` self-clears (`i2c_ll_master_clr_bus`).
- `FILTER_CFG` (0x50), `CLK_CONF` (0x54), `SCL_ST_TIME_OUT` (0x78),
  `SCL_MAIN_ST_TIME_OUT` (0x7c), `SCL_STRETCH_CONF` (0x84) read back what
  was written.
- FIFO also reachable through 0x100/0x180 windows; `DATA` stays at 0x1c.
- Interrupt bits NACK (10), TRANS_COMPLETE (7), END_DETECT (3) keep the
  ESP32 positions. Full mask 0x3fff.
- Transactions longer than 8 commands: the driver ends a batch with END,
  reloads COMD on END_DETECT, then sets TRANS_START again. The model must
  resume from the reloaded list without resetting bus state.

Verify: a qtest (or a small firmware) that writes a command list and checks
DONE bits, `INT_RAW` and FIFO contents.

### I3. Wire into `esp32s3.c`

- `object_initialize_child` for `i2c[ESP32S3_I2C_COUNT]` in the SoC init.
- `sysbus_realize`, map at `DR_REG_I2C_EXT_BASE` / `DR_REG_I2C1_EXT_BASE`
  with `memory_region_add_subregion_overlap(..., 0)`.
- `sysbus_connect_irq` to `ETS_I2C_EXT0_INTR_SOURCE + i` on the intmatrix.
- Reset both in `esp32s3_soc_reset` (reference: `hw/xtensa/esp32.c:184-185`).

Verify: `info mtree` shows both regions above the iomem catch-all.

### I4. Default slave

The I2C bus is not on `sysbus-default`, so slaves cannot be added with
`-device`. Attach `tmp105@0x48` on I2C0 with `i2c_slave_create_simple`,
as `esp32_machine_init_i2c` does (`hw/xtensa/esp32.c:742-755`).
`TMP105` is already selected by `XTENSA_ESP32S3`.

### I5. Verify

- IDF 5.5.3 `examples/peripherals/i2c/i2c_tools`: `i2cdetect` finds 0x48.
- Read `tmp105` with both the legacy driver (`driver/i2c.h`) and the new
  driver (`driver/i2c_master.h`).
- One transfer longer than 8 commands (exercises END -> reload).
- Re-run the ESP32 I2C firmware for regression.
- To confirm while implementing: whether `i2c_master` blocking transfers
  depend on the ISR. If they do, the IRQ wiring in I3 is mandatory.

## Phase 2: LEDC (about 2 days)

### L1. New model `hw/misc/esp32s3_ledc.c`

- Timer n `CONF` at `0xA0+8n`: `duty_res` bits 0-3 (max 14), `para_up`
  bit 25. `VALUE` at `0xA4+8n`.
- Channel n at `0x14*n`: `CONF0` (+0, `timer_sel` bits 0-1, `para_up`
  bit 4), `HPOINT` (+4), `DUTY` (+8, 4 fractional bits), `CONF1` (+0xC,
  `duty_start` bit 31), `DUTY_R` (+0x10).
- Apply duty on `para_up`/`duty_start`. `DUTY_R` returns the current duty
  (the ESP32 model returns 0, which breaks `ledc_get_duty`).
- `INT_RAW/ST/ENA/CLR` at 0xC0/0xC4/0xC8/0xCC, `CONF` at 0xD0, `DATE`
  at 0xFC.
- Output: 8 `TYPE_LED` children, `led_set_intensity` as in the ESP32 model.

### L2. Instant fade

When a fade is started, set the final duty immediately and raise
`DUTY_CHNG_END_CHn`. Without it `ledc_fade_start(..., LEDC_FADE_WAIT_DONE)`
hangs. Check bit positions against `soc/esp32s3/include/soc/ledc_reg.h`.

### L3. Wire into the machine

- Map at `DR_REG_LEDC_BASE`, IRQ to `ETS_LEDC_INTR_SOURCE`.
- `hw/xtensa/Kconfig`: add `select LED` to `XTENSA_ESP32S3`.
- `hw/misc/meson.build`: build `esp32s3_ledc.c` for `CONFIG_XTENSA_ESP32S3`.

### L4. Verify

- `ledc_basic`: `-trace led_set_intensity` shows the expected percentage.
- `ledc_fade`: no hang, final duty correct, `ledc_get_duty` matches.

## Phase 3: WiFi (needs a decision)

| Option | What | Effort | Risk |
|---|---|---|---|
| **A. Port lcgamboa WiFi emulation** | lcgamboa/qemu (GPL, compatible) has STA, SoftAP and ESP-NOW for ESP32 and C3. No S3 port found. Needs MAC registers, DMA descriptors and BB/FE/RF-calibration stubs the closed blobs touch. | Weeks | High: blobs change per IDF release, likely needs a pinned IDF version. |
| **B. Use existing `open_eth`** | Firmware uses `esp_eth` + `esp_netif`. TCP/IP, MQTT, HTTP testable. | None in QEMU | `esp_wifi_*` APIs not testable. |
| **C. Firmware-side shim** | Component that fakes basic `esp_wifi_*` (init/start/connect/events) on top of `open_eth`, enabled only for QEMU builds. | 2-4 days | Real WiFi driver path not tested. |

**Recommendation:** B or C now so networked apps run. In parallel, a
1-2 day spike for A (W2): clone lcgamboa/qemu, list every register,
interrupt source and blob hook `esp32c3_wifi` touches, and map each to S3
(`DR_REG_FE2_BASE` 0x60005000, `FE` 0x60006000, `NRX` 0x6001CC00,
`BB` 0x6001D000, analog I2C). Decide on W3 from the spike result.

If W3 goes ahead, resolve constraint 3 first: move `open_eth` to a free
interrupt source or make it mutually exclusive with the WiFi NIC.

## Delivery

- Order: I2C -> LEDC -> WiFi spike. One branch per phase, one commit per
  step ID, update the Progress table in the same commit.
- Every step has a firmware run:
  `qemu-system-xtensa -M esp32s3 -nographic -drive file=flash.bin,if=mtd,format=raw`.
  Add qtests where register-level behaviour should be locked.

## What could break

- **ESP32 machine I2C (I1):** a wrong default in the parametrized model
  breaks existing ESP32 firmware. Noticed by the I5 regression run.
- **Memory map priority (I3, L3):** a region added with the wrong priority
  stays hidden behind the iomem catch-all; reads return 0 and drivers time
  out. Noticed via `info mtree` or I2C timeout errors in the IDF log.
- **Interrupt wiring:** a wrong source number gives no ISR, so `i2c_master`
  or LEDC fade blocks forever. Noticed as a task watchdog trigger.
- **`open_eth` (W3):** changing its interrupt source breaks existing
  Ethernet firmware for S3.

## Sources

- [lcgamboa/qemu - esp32_wifi_ap.c](https://github.com/lcgamboa/qemu/blob/7a976c88ebf7338f25b7079d8595d262fb48cc43/hw/misc/esp32_wifi_ap.c)
- [espressif/qemu PR #80](https://github.com/espressif/qemu/pull/80)
- [esp32-open-mac/qemu](https://github.com/esp32-open-mac/qemu)
