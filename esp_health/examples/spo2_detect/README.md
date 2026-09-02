# Blood Oxygen Saturation Detect Example (SpO2)

- [中文版本](./README_CN.md)
- Difficulty: ⭐⭐

## Overview

This example uses a **MAX30102** PPG sensor and **esp-health** (`esp_health_spo2`) to demonstrate real-time dual-channel SpO2 measurement under static / low-motion conditions. It acquires synchronous **IR** and **Red** PPG over I2C, estimates SpO2 on 5 s windows, and prints results to the serial monitor.

- Board bring-up is handled by [ESP Board Manager](https://github.com/espressif/esp-board-manager).
- SpO2 estimation uses `esp_health_spo2_open` / `esp_health_spo2_process` / `esp_health_spo2_reset` / `esp_health_spo2_close`.
- Wear detection, last-OK hold, and `reset` after finger-off live in the **application layer**. The library has no motion artifact suppression.
- Default config is `ESP_HEALTH_SPO2_CFG_DEFAULT()` (MAX30102-class 660 nm / 880 nm); it is not medical-grade.

### Typical scenarios

- Finger-clip still contact on MAX30102; watch serial SpO2 logs.
- Use as an integration reference for `esp_health_spo2`: acquisition task + process task + `reset` on a new contact session.

### How it runs

```mermaid
graph LR
    MAX30102 --> Acq["Acquisition task"]
    Acq --> Ring["5 s ring buffer"]
    Ring --> Proc["Process task"]
    Proc --> SpO2["esp_health_spo2_process"]
    SpO2 --> UART["Serial log"]
```

- Acquisition task: reads IR + Red chunks from MAX30102 (`CHUNK_SAMPLES = 32`) and updates the wear flag. Finger-off or ~1 s of consecutive FIFO/I2C failures clears the session, drops the last SpO2, and marks that `reset` is needed. FIFO overflow is a stream gap: the ring is dropped and a new full window must be collected.
- Process task: copies a 5 s IR/Red window **only when new samples have arrived**, calls `esp_health_spo2_reset()` on the **same task** when needed, then `esp_health_spo2_process` (the handle is not thread-safe). If finger-off or FIFO overflow happens while `process()` is running, the result is discarded. Displayed SpO2 is updated only when the return is `ESP_HEALTH_ERR_OK` and `spo2` is in 70–100. If this window is invalid but `quality_ratio` is still usable, the last valid SpO2 is kept. A stalled FIFO does not republish the previous window. Other tasks should read results via `ppg_spo2_get_measurement()` (mutex snapshot).

## Environment

### Hardware

- **Board**: ESP32-S3-DevKitC by default, with custom board `esp32_s3_devkit_max30102`.
- **Sensor**: MAX30102 PPG module.
- **Wiring** (reference board `esp32_s3_devkit_max30102`):

| MAX30102 | ESP32-S3 GPIO |
|----------|---------------|
| SDA      | GPIO19        |
| SCL      | GPIO20        |
| INT      | GPIO21        |
| VIN / GND | 3V3 / GND   |

### Default IDF branch

This example is verified on IDF v6.x (CI: release/v6.1). Use an IDF version compatible with `espressif/esp_board_manager`.

### Prerequisites

This example uses a custom board and a MAX30102 driver amend. Run `idf.py bmgr` to generate board code before building. IR and Red must be acquired synchronously with **positive DC**.

## Build and Flash

### Setup

Before building, make sure the ESP-IDF environment is configured. If already configured, skip this block and enter the project directory. If not, run the following scripts in the ESP-IDF root directory. For complete steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html).

```bash
./install.sh
. ./export.sh
```

Simplified steps:

- Enter the example directory:

```bash
cd $YOUR_ESP_HEALTH_PATH/examples/spo2_detect
```

This example uses [ESP Board Manager](https://github.com/espressif/esp-board-manager). Install [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) as the recommended entry.

- Install it in the active ESP-IDF Python environment (once per environment):

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist  # only when an update is required
```

- Select the custom board that includes MAX30102:

```bash
idf.py bmgr -b esp32_s3_devkit_max30102 -a components/amend/max30102
```

  The first `idf.py bmgr` run downloads `espressif/esp_board_manager` from `main/idf_component.yml`. Generated code is written to `components/gen_bmgr_codes/`.

The MAX30102 module in this example can be reused on other ESP Board Manager boards via `-a/--amend`. Adjust a few settings for the target board wiring and pass this amend directory when generating board code. For `-a/--amend`, see [Board Amend](https://docs.espressif.com/projects/esp-board-manager/en/latest/create-board/amend.html). For the settings to change, see [MAX30102 configuration](components/amend/max30102/README.md).

> [!NOTE]
> For a custom board, see the [Create Board Guide](https://docs.espressif.com/projects/esp-board-manager/en/latest/create-board/index.html).
> More on `esp_board_manager`: [Getting Started](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README.md).

`esp_health` is the local component, declared in `main/idf_component.yml` with `override_path: "../../.."`.

### Project configuration

This example has no extra feature options; menuconfig is usually unnecessary. Shared Kconfig is in `sdkconfig.defaults`; the ESP32-S3 overlay is `sdkconfig.defaults.esp32s3` (also `.esp32p4` / `.esp32s31`). To change log level or similar:

```bash
idf.py menuconfig
```

> Press `s` to save, then `Esc` to exit.

### Build and flash

- Build:

```bash
idf.py build
```

- Flash and open the serial monitor (replace `PORT` with your serial port):

```bash
idf.py -p PORT flash monitor
```

- Exit the monitor with `Ctrl-]`

## How to Use

### Features and usage

1. After flash, keep a finger **still** on the MAX30102 optical window, covering both IR and red LEDs.
2. Wait about 5 s for the first analysis window.
3. Serial output shows `spo2` / `corr` / `q`; lifting the finger prints `finger off`.
4. After finger-off then finger-on, the process task calls `esp_health_spo2_reset()` before `process` so periodicity state from the previous session is not reused.

### Log output

Flow is init, acquisition/process tasks, first-window buffering, then SpO2. Key logs:

```text
I (xxx) PPG_SPO2: Init board + MAX30102 + esp_health_spo2
I (xxx) PPG_SPO2: acq task running
I (xxx) PPG_SPO2: proc task running
I (xxx) PPG_SPO2: Collecting first 5s window...
I (xxx) PPG_SPO2: buffering 32/500 (keep finger on sensor)
I (xxx) PPG_SPO2: SpO2: spo2=98.2% corr=0.97 q=0.82 err=0
I (xxx) PPG_SPO2: SpO2: --  (finger off)
I (xxx) PPG_SPO2: SpO2: --  (sensor lost)
```

- `Collecting first 5s window...`: filling the first analysis window
- `spo2=NN%`: valid SpO2 for this window
- `finger off`: wear/contact check reports finger off
- `sensor lost`: consecutive FIFO/I2C failures; last SpO2 is dropped, process waits for a new full window

### Key parameters (`main/ppg_spo2.c`)

| Macro / API | Value | Meaning |
|-------------|------:|---------|
| `SAMPLE_RATE` | 100 | PPG sample rate (Hz), supported range 16–1000 |
| `WINDOW_SEC` | 5 | Analysis window (s) |
| `CHUNK_SAMPLES` | 32 | Samples per FIFO read |
| `SPO2_ACQ_FAIL_INVALIDATE` | 50 | Consecutive FIFO fails (~1 s) before dropping the session |
| `cfg` | `ESP_HEALTH_SPO2_CFG_DEFAULT()` | Default MAX30102-class config (example overrides gates) |
| SpO2 range | 70–100 | Valid SpO2 from `esp_health_spo2` |

### Code structure

```text
spo2_detect/
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32s3      # also .esp32p4 / .esp32s31
├── README.md / README_CN.md
├── main/
│   ├── ppg_spo2.c / ppg_spo2.h # Application entry and SpO2 pipeline
│   ├── spo2_freshness.h        # Skip stale windows; drop session on I2C loss
│   ├── spo2_meas_pub.h         # Mutex snapshot for ppg_spo2_get_measurement
│   ├── CMakeLists.txt
│   └── idf_component.yml       # esp_board_manager + local esp_health override
└── components/
    ├── board_customer/         # Custom board: esp32_s3_devkit_max30102
    ├── amend/max30102/         # MAX30102 driver + board amend
    └── gen_bmgr_codes/         # Generated by idf.py bmgr; do not edit by hand
```

## Troubleshooting

### MAX30102 init fails or FIFO read fails

If I2C / soft reset fails or logs show `FIFO read fail`, check SDA=GPIO19, SCL=GPIO20, INT=GPIO21, 3V3 power, and sensor address 0x57. IR and Red must stay synchronous with positive DC.

### `spo2` is 0 or frequent finger off

Keep the finger still and covering the optical window. Noisy, aperiodic, or out-of-range windows still return `ESP_HEALTH_ERR_OK` with `spo2 = 0`. Recalibrate `esp_health_spo2_calib_t` for other wavelengths or optics.

### `idf.py bmgr` cannot find the board

Run it from the example directory: `idf.py bmgr -b esp32_s3_devkit_max30102 -a components/amend/max30102`. The first run needs access to the component registry to download `esp_board_manager`.

## Technical Support

Please use the following link for support:

- Bugs and feature requests: [GitHub issue](https://github.com/espressif/esp-health/issues)

We will reply as soon as possible.
