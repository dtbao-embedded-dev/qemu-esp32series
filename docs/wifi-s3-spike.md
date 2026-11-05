# Spike W2: what the ESP32-S3 WiFi/PHY blobs need from QEMU

Step W2 of `docs/plan-esp32s3-i2c-ledc-wifi.md`. Goal: find which registers
the closed ESP-IDF v6.1 blobs (`libphy.a`, `libpp.a`, `libnet80211.a`) use on
the ESP32-S3 during `esp_wifi_init()`, `esp_wifi_start()` and a scan, and
whether the lcgamboa/qemu WiFi emulation (branch `picsimlab-esp32`, commit
64aed503) can be ported.

**Decision: GO.** With register stubs only, `esp_wifi_start()` returns and a
scan completes. The PHY addresses and stub values match lcgamboa's ESP32-C3
models, and the S3 WiFi MAC DMA registers use the C3 layout at base
0x60033000.

## Method

- Test app: `tests/esp-idf/wifi_s3` (IDF v6.1). It runs init, start, scan,
  connect to the open AP `Espressif`, waits for an IP and serves one HTTP
  request. Each step prints `WIFI_TEST <step> OK|FAIL|TIMEOUT`.
- Instrumentation (local patch, not committed): the catch-all
  `esp32s3.iomem` handlers log every access, and the read value per address
  range comes from an environment variable. The policies are:
  - `zero`;
  - `ones`;
  - `store`: return the last written value;
  - `store1`: return the last written value, or all ones if never written;
  - `usec`: return the virtual time in microseconds;
  - a constant.
- Hang points: sample `PC`/`A0` from the QEMU monitor (`info registers`),
  resolve with `addr2line` against the app ELF and
  `esp32s3_rev0_rom.elf`, then disassemble the loop.

## Baseline (current machine, no stubs)

```
WIFI_TEST init OK
WIFI_TEST start: calling esp_wifi_start
I (223) phy_init: phy_version 712,87e8c20e,Apr 13 2026,18:51:10
W (223) phy_init: failed to load RF calibration data (0x1102), falling back to full calibration
```

After that there is no further output. CPU0 spins in ROM `rom_pkdet_vol_start`
(PC 0x40036a44), called from `ram_read_sar2_code` in `rc_cal`. It reads
0x6000e050 and waits for bits [26:24] == 7. The catch-all region returns 0,
so the loop never exits.

## Wait loops found, in order

| # | Function (caller) | Register | Waits for | Stub value |
|---|---|---|---|---|
| 1 | ROM `rom_pkdet_vol_start` (`rc_cal`) | 0x6000e050 (I2C_MST/ANA) | bits [26:24] == 7 | 0x07000000 |
| 2 | `wait_freq_set_busy` (`set_chan_freq_sw_start`) | 0x6000e168, timeout from 0x60035000 | bit 31 clear, or 1000 us elapsed | 0x6000e168 = 0; 0x60035000 = free-running us counter |
| 3 | `temperature_sensor_ll_get_raw_value` (`temperature_sensor_hal_get_degree`) | 0x60008850 `SENS_SAR_TSENS_CTRL_REG` | `TSENS_READY` (bit 8) | 0x168 (ready, raw 0x68, about 25 C) |
| 4 | `ram_iq_est_enable` (ROM `rom_dc_iq_est`) | 0x60006174 (FE) | bit 16 | all ones (lcgamboa FE 0x174) |
| 5 | MAC TX setup | 0x60033d14 (`DMA_OUT_STATUS`) | non-zero | 1 (lcgamboa C3) |

After #1-#5, PHY full calibration completes (`phy_init: Saving new
calibration data`). The output then continues:

```
I (279) wifi:mode : sta (00:00:00:00:00:00)
I (279) wifi:enable tsf
WIFI_TEST start OK
WIFI_TEST scan count=0
W (7819) wifi:TX Q not empty: 500, TXQ_BLOCK=0
```

## Stub map that gets `esp_wifi_start()` through

Ranges are listed in priority order; the first match wins.

| Block | Address | Behaviour | Source |
|---|---|---|---|
| I2C_MST / ANA | 0x6000e000, 0x1000 | store; 0x00, 0x48 = 0x00ffffff; 0x04 = 0xfdffffff; 0x44, 0x4c, 0xc4 = all ones; 0x50 = 0x07000000; **0x168 = 0** | lcgamboa `esp32c3_ana.c` (same base on C3) + S3 #2 |
| FE | 0x60006000, 0x1000 | store; 0x174, 0x7c = all ones | lcgamboa `esp32_fe.c` |
| FE2 | 0x60005000, 0x1000 | zero | lcgamboa C3 unimp |
| NRX | 0x6001c000, 0x1000 | all ones | lcgamboa C3 unimp (`DR_REG_NRX_BASE - 0xc00`) |
| BB | 0x6001d000, 0x1000 | all ones | lcgamboa C3 unimp |
| SENS TSENS | 0x60008850 | 0x168 | S3 #3 |
| MAC | 0x60032000-0x60035fff | store; 0x60033cb0 = 1; 0x60033d14 = 1; 0x60035000 = us counter | lcgamboa C3 + S3 #2 |

`ana` writes to 0x150 select the channel in lcgamboa (`(bits[27:20] - 7) / 5`).
The S3 blob writes the same block, so the same decode is expected to apply;
T3 will confirm this when the scan reaches channels 1, 5 and 10.

## WiFi MAC layout on the S3

The registers the S3 blob used during start match the C3/ESP32 layout of
lcgamboa (`include/hw/misc/esp32c3_wifi.h`, `esp32_wifi.h`), with the base
at 0x60033000. Values below are the ones written in this run.

| Offset | lcgamboa name | Observed |
|---|---|---|
| 0x000-0x00c, 0x040-0x04c | BSSID / MAC address | written (MAC 0 because eFuse is blank) |
| 0x060-0x06c | MAC filter | written 0xffffffff |
| 0x084 | `DMA_IN_STATUS` | read |
| 0x088 | `DMA_INLINK` | written with the RX descriptor (0x3fceebf8) |
| 0x0d8, 0x0dc | `RX_POLICY` | written 0x40 |
| 0xc40 | `DMA_INT_CLR` | written 0xffffffff |
| 0xc64 | `OFFSET_REG` | written 0x3fc00000 (DMA address base, as on C3) |
| 0xcb0 | `STATUS` | read, must be 1 |
| 0xd04 | `TX_CONFIG` | written 0x77 |
| 0xd08 | `DMA_OUTLINK` | written 0xc06acea4 (start bits + descriptor) |
| 0xd14 | `DMA_OUT_STATUS` | read, must be 1 |

The blob also touches 0x60034100-0x60034318 (per-queue TX registers in steps
of 0x4c) and 0x60035000-0x6003509c (0x60035000 is the us timer). The
existing `misc.esp32s3.rng` maps 0x6003507c, inside this range, so the MAC
model must leave that word to the RNG.

The RX header is `wifi_pkt_rx_ctrl_t`. IDF v6.1 uses the same layout for S3
and C3 (`esp_wifi/include/local/esp_wifi_types_native.h`, the
`ESP32S3 || ESP32C3 || ESP32C2` branches), so lcgamboa's
`wifi_pkt_rx_ctrl_c3_t` applies.

## What is still missing (for T2-T4)

- **TX completion.** `TX Q not empty: 500` shows that no TX-done interrupt
  arrives. lcgamboa raises 0x80 in the interrupt status after a write to
  `DMA_OUTLINK` and on `Esp32_WLAN_frame_delivered`.
- **RX path and the emulated AP.** Frames are written into the descriptor
  from `DMA_INLINK`, then RX interrupt bits 0x1004000 are raised on
  `ETS_WIFI_MAC_INTR_SOURCE`.
- **MAC address.** The eFuse image is blank, so the station MAC is
  00:00:00:00:00:00. A frame with a zero source address will not reach
  `match_mac_address()`. T3 must give the station a real MAC, either by
  programming the eFuse MAC or by documenting an eFuse image with one.
- **Not checked by this spike:**
  - the PHY status word lcgamboa writes per packet (`Esp32_WLAN_Set_Packet_Status`, PHYA offset 0x320 on C3);
  - the analog channel decode;
  - interrupt bit positions.

  All three show up in T3 as scan results.

## Revised plan for T2-T4

The approach is unchanged; only the file list is more precise.

- **T2 (PHY and MAC skeleton).**
  - `hw/misc/esp32s3_wifi_phy.c`: one device with MMIO regions for ANA (0x6000e000), FE (0x60006000), FE2, NRX, BB and the TSENS word, using the values above.
  - `hw/misc/esp32s3_wifi.c`: MAC register file over 0x60032000-0x60035fff (except the RNG word), with `STATUS`/`DMA_OUT_STATUS` = 1 and the us timer.
  - Both are created only when `-nic ...,model=esp32s3_wifi` is given. A machine without that NIC is unchanged, so regi2c reads at 0x6000e000 during boot keep their current behaviour.
- **T3 (scan).**
  - Port `esp32_wifi_ap.c`, `esp32_wlan_packet.c/.h`, `esp32_wlan.h`, `esp32_wifi.h`.
  - Add DMA RX/TX and interrupts in `esp32s3_wifi.c`, plus the channel decode from ANA 0x150.
  - Give the station a non-zero MAC.
- **T4.** Unchanged: connect, DHCP, two-way TCP, NIC exclusivity, docs.

## Reproducing

The instrumentation patch and the final policy string are kept outside the
repository; they are not needed to build the models. The policy that reaches
`WIFI_TEST start OK` is:

```
60033d14:4:v00000001,60033cb0:4:v00000001,6000e168:4:zero,60035000:4:usec,
60008850:4:v00000168,6000e000:4:v00ffffff,6000e048:4:v00ffffff,
6000e004:4:vfdffffff,6000e044:4:ones,6000e04c:4:ones,6000e0c4:4:ones,
6000e050:4:v07000000,6000e000:1000:store,60006174:4:ones,6000607c:4:ones,
60006000:1000:store,6001d000:1000:ones,6001c000:1000:ones,60005000:1000:zero,
60032000:4000:store
```
