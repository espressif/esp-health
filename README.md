# ESP Health

- [中文](./README_CN.md)

`esp-health` is a modular health-sensing algorithm library for Espressif SoCs. It provides reusable, composable algorithms for wearables, health-monitoring terminals, and similar products. The project focuses on sensor-signal processing, feature extraction, and health-metric estimation. Each algorithm is packaged as an independent component that can be integrated as needed, with source, dependency metadata, API docs, examples, and tests.

The repository currently centers on `esp_health`. It provides lightweight modules for resource-constrained MCUs, with a consistent API for products such as bands, watches, and finger-clip devices. Currently supported modules include static PPG heart-rate detection (HR) and dual-channel PPG blood-oxygen detection (SpO2). Requires **ESP-IDF v5.5 or later**.

## Components

| Component | Status | Purpose | Documentation and tests |
| --- | --- | --- | --- |
| [`esp_health`](./esp_health) | v0.1.0 | PPG heart-rate and SpO2 algorithms for Espressif SoCs | [English documentation](./esp_health/README.md) · [中文](./esp_health/README_CN.md) · [Examples](./esp_health/examples) · [Test applications](./esp_health/test_apps) |

## Getting Started

### Add the Component from ESP Component Registry

Run the following command in your ESP-IDF project:

```bash
idf.py add-dependency "espressif/esp_health"
```

See [`idf_component.yml`](./esp_health/idf_component.yml) and the component documentation for supported targets, dependencies, and optional features.

### Integrate from Source

Place the `esp_health` directory under your ESP-IDF project's `components/` directory, then follow its README to call the algorithm APIs. For module details, performance, and hardware examples, see:

- [`esp_health` documentation](./esp_health/README.md)
- [`heart_rate_detect` example](./esp_health/examples/heart_rate_detect)
- [`spo2_detect` example](./esp_health/examples/spo2_detect)
- [`test_apps`](./esp_health/test_apps)

## Repository Layout

```text
esp-health/
├── esp_health/   # Health algorithm component, docs, examples, and tests
└── tools/        # Repository test and CI tools
```

Future health components will be added as independent top-level directories. Each component owns its source, metadata, documentation, and tests, while the repository root remains the entry point for discovery and navigation.

## Feedback and Contributions

- Report problems and feature requests through [GitHub Issues](https://github.com/espressif/esp-health/issues).
- Contributions of algorithms, examples, tests, and documentation improvements are welcome.
- New components should use independent top-level directories and update both root component lists.
