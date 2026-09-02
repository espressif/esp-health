# ESP_HEALTH

- [![Component Registry](https://components.espressif.com/components/espressif/esp_health/badge.svg)](https://components.espressif.com/components/espressif/esp_health)

- [中文版](./README_CN.md)

Espressif Health (ESP_HEALTH) is Espressif’s health-sensing algorithm package for SoCs. It processes sensor sample streams supplied by the application and estimates physiological indicators such as heart rate and blood oxygen. The library does not handle hardware acquisition or drivers. It provides lightweight modules suited to resource-constrained MCUs, with a consistent, easy-to-integrate API for products such as bands, watches, and finger-clip devices. Modules can be combined as needed; motion artifact suppression, signal-quality checks, and similar product-specific logic can be layered in the application. Currently supported modules include static PPG heart-rate detection (HR) and dual-channel PPG blood-oxygen detection (SpO2). Requires **ESP-IDF v5.5 or later**.

# Quick Start

Add this component to your ESP-IDF project:

```bash
idf.py add-dependency "espressif/esp_health"
```

Alternatively, copy the `esp_health` directory into your project's `components/` folder. Then require it in CMake:

```cmake
idf_component_register(
    ...
    REQUIRES esp_health
)
```

Hardware examples:

- [Heart rate detect](examples/heart_rate_detect)
- [SpO2 detect](examples/spo2_detect)

# Detailed Introduction of Each Module

The following table lists the sample rate and input format supported by each module. For feature details, performance figures, examples, and more, follow the README link in the `Module` column.

| Module | Sample Rate (Hz) | Input Format | Notes | Initial Supported Version |
|:------:|:----------------:|:------------:|:------|:-------------------------:|
| [Heart Rate Detection (HR)](docs/README_HR.md) | 16–1000 (typical 64 / 100) | `float` PPG blocks | Static / low-motion peak HR; API: `esp_health_hr_open` / `process` / `close` | v0.1.0 |
| [Blood Oxygen Saturation Detection (SpO2)](docs/README_SPO2.md) | 16–1000 (typical 64 / 100) | Synchronous `float` IR + Red blocks | Static / low-motion ratio-of-ratios SpO2; API: `esp_health_spo2_open` / `process` / `reset` / `close` | v0.1.0 |

# Release and SoC Compatibility

v0.1.0 ships prebuilt libraries for ESP32, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-C61, ESP32-P4, ESP32-H4, and ESP32-S31. The HR and SpO2 modules are primarily software (float / fixed-point) and do not require a dedicated peripheral. ESP-IDF **v5.5+** is required (`esp_log`).
