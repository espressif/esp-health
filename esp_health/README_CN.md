# ESP_HEALTH（健康感知算法库）

- [![Component Registry](https://components.espressif.com/components/espressif/esp_health/badge.svg)](https://components.espressif.com/components/espressif/esp_health)

- [English](./README.md)

Espressif Health（ESP_HEALTH）是乐鑫面向 SoC 的健康感知算法组件集合，对应用层送入的传感器采样数据做信号处理，估算心率、血氧等生理指标。组件本身不负责硬件采集与驱动；提供面向 MCU 资源受限场景的轻量算法模块，接口统一、易于集成，可在手环、手表、指夹等健康相关产品中复用。各模块可按需组合使用，复杂场景下的运动伪迹抑制、信号质量判断等可在应用层按产品需求叠加。当前支持的模块包括：静止 PPG 心率检测（HR）、双通道 PPG 血氧饱和度检测（SpO2）。需要 **ESP-IDF v5.5 及以上**。

# 快速开始

在 ESP-IDF 工程中添加本组件：

```bash
idf.py add-dependency "espressif/esp_health"
```

也可以将 `esp_health` 目录放入工程的 `components/` 下，并在 CMake 中声明依赖：

```cmake
idf_component_register(
    ...
    REQUIRES esp_health
)
```

硬件例程：

- [心率检测](examples/heart_rate_detect)
- [血氧检测](examples/spo2_detect)

# 各模块详细介绍入口

下表列出了各模块支持的采样率与输入格式。若需了解功能介绍、性能指标、使用示例等，请点击 `模块` 列中的 README 链接进入相应文档。

| 模块 | 采样率（Hz） | 输入格式 | 说明 | 初始支持版本 |
|:----:|:------------:|:--------:|:-----|:------------:|
| [心率检测（HR）](docs/README_HR_CN.md) | 16–1000（常用 64 / 100） | `float` PPG 样本块 | 静止 / 低运动峰值法心率；接口：`esp_health_hr_open` / `process` / `close` | v0.1.0 |
| [血氧饱和度检测（SpO2）](docs/README_SPO2_CN.md) | 16–1000（常用 64 / 100） | 同步 `float` IR + Red 样本块 | 静止 / 低运动比值比血氧；接口：`esp_health_spo2_open` / `process` / `reset` / `close` | v0.1.0 |

# 版本发布与 SoC 兼容性

v0.1.0 已提供 ESP32、ESP32-S2、ESP32-S3、ESP32-C2、ESP32-C3、ESP32-C5、ESP32-C6、ESP32-C61、ESP32-P4、ESP32-H4、ESP32-S31 的预编译库。HR、SpO2 以纯软件浮点 / 定点处理为主，无专用外设依赖。需要 ESP-IDF **v5.5+**（`esp_log`）。
