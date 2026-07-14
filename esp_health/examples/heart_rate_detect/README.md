# Heart Rate Detect Example

- [中文版本](./README_CN.md)
- Difficulty: ⭐⭐

## Overview

This example uses a **MAX30102** PPG sensor and **esp-health** (`esp_health_hr`) to demonstrate real-time heart-rate measurement under static / low-motion conditions. It acquires RED-channel PPG over I2C, estimates BPM on sliding windows, and prints results to the serial monitor.

- Board bring-up is handled by [ESP Board Manager](https://github.com/espressif/esp-board-manager).
- Heart-rate estimation uses `esp_health_hr_open` / `esp_health_hr_process` / `esp_health_hr_close`.
- Wear detection, EMA smoothing, and hold-on-fail live in the **application layer**. The library has no motion artifact suppression.

### Typical scenarios

- Keep a finger or wrist still on MAX30102 and watch serial HR logs.
- Use as an integration reference for `esp_health_hr`: data task + algorithm task + light post-processing.

### How it runs

```mermaid
graph LR
    MAX30102 --> Data["Data task"]
    Data --> Queue["FreeRTOS queue"]
    Queue --> Algo["Algorithm task"]
    Algo --> HR["esp_health_hr_process"]
    HR --> UART["Serial log"]
```

- Data task: reads one second of RED PPG (`SAMPLE_RATE = 100`) from MAX30102 and posts samples plus wear / FIFO-gap in the same queue item (no shared `detect_flag`). FIFO overflow is a stream gap: queued samples and the analysis ring are dropped.
- Algorithm task: fills a ring buffer (up to about 15 s). First `esp_health_hr_process` runs at **4 s**, then every 1 s, with the analysis window capped at **8 s** (header recommends 8–15 s). Displayed BPM is updated only when the return is `ESP_HEALTH_ERR_OK` and `avg_bpm` is in 40–200; otherwise the last valid BPM is held briefly. Harmonic correction is done inside the library.

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

This example uses a custom board and a MAX30102 driver amend. Run `idf.py bmgr` to generate board code before building.

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
cd $YOUR_ESP_HEALTH_PATH/examples/heart_rate_detect
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

1. After flash, keep a finger **still** on the MAX30102 optical window.
2. Wait a few seconds for warmup and window fill.
3. Serial output shows a valid BPM; lifting the finger prints `finger off`.
4. Motion, loose contact, or ambient light leak can yield `measuring` / `hold`, or large BPM jumps.

### Log output

Initialization is marked `[ 1 ]`–`[ 4 ]`, then heart rate is printed periodically. Key logs:

```text
I (xxx) PPG_HEART_RATE: [ 1 ] Initialize Board Manager
I (xxx) PPG_HEART_RATE: [ 2 ] Initialize MAX30102 PPG device
I (xxx) PPG_HEART_RATE: [ 3 ] Initialize PPG queue and algorithm
I (xxx) PPG_HEART_RATE: [ 4 ] Start data + algorithm tasks
I (xxx) PPG_HEART_RATE: Heart Rate: -- (measuring)
I (xxx) PPG_HEART_RATE: Heart Rate: -- (win=Ns, measuring)
I (xxx) PPG_HEART_RATE: Heart Rate: 72 BPM (win=8s)
I (xxx) PPG_HEART_RATE: Heart Rate: 72 BPM (win=8s, hold)
I (xxx) PPG_HEART_RATE: Heart Rate: -- (finger off)
```

- `-- (measuring)`: warmup, window too short, or signal not yet stable
- `NN BPM`: estimated heart rate (EMA-smoothed in the example)
- `hold`: no valid reading this window; last BPM kept briefly
- `finger off`: wear/contact check reports finger off

### Key parameters (`main/ppg_heart_rate.c`)

| Macro | Value | Meaning |
|-------|------:|---------|
| `SAMPLE_RATE` | 100 | PPG sample rate (Hz), supported range 16–1000 |
| `RING_SEC` | 15 | Ring buffer length (s) |
| `ANALYSIS_SEC` | 8 | Analysis window (s), cap |
| `HR_EXAMPLE_MIN_WINDOW_SEC` | 4 | First `process` once this many seconds are in the ring |
| `UPDATE_MS` | 1000 | HR update period (ms) |
| BPM range | 40–200 | Valid BPM from `esp_health_hr` |

### Code structure

```text
heart_rate_detect/
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32s3      # also .esp32p4 / .esp32s31
├── README.md / README_CN.md
├── main/
│   ├── ppg_heart_rate.c        # Application entry and HR pipeline
│   ├── hr_example_win.h        # First process at 4 s, 1 s hop, 8 s cap
│   ├── hr_ppg_msg.h            # Queue item kind: samples / finger-off / FIFO gap
│   ├── CMakeLists.txt
│   └── idf_component.yml       # esp_board_manager + local esp_health override
└── components/
    ├── board_customer/         # Custom board: esp32_s3_devkit_max30102
    ├── amend/max30102/         # MAX30102 driver + board amend
    └── gen_bmgr_codes/         # Generated by idf.py bmgr; do not edit by hand
```

## Troubleshooting

### MAX30102 init fails

If I2C / soft reset fails, check SDA=GPIO19, SCL=GPIO20, INT=GPIO21, 3V3 power, and sensor address 0x57.

### Stuck on measuring, or BPM jumps

Keep the finger still and covering the optical window. `esp_health_hr` has no motion artifact suppression; for walking / running, add ACC gating in the application.

### `idf.py bmgr` cannot find the board

Run it from the example directory: `idf.py bmgr -b esp32_s3_devkit_max30102 -a components/amend/max30102`. The first run needs access to the component registry to download `esp_board_manager`.

## Technical Support

Please use the following link for support:

- Bugs and feature requests: [GitHub issue](https://github.com/espressif/esp-health/issues)

We will reply as soon as possible.
