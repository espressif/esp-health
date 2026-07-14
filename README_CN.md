# ESP Health

- [English](./README.md)

`esp-health` 是面向乐鑫 SoC 的模块化健康感知算法库，为可穿戴设备、健康监测终端等应用场景提供可复用、可组合的算法能力。项目专注于传感器数据的信号处理、特征提取与健康指标计算。各算法以独立组件形式组织，可按需集成，并配套完整的源码、依赖声明、接口文档、示例程序和测试用例。

当前仓库主要提供 `esp_health`：面向 MCU 资源受限场景的轻量算法模块，接口统一，可用于手环、手表、指夹等产品。当前支持静止 PPG 心率检测（HR）和双通道 PPG 血氧饱和度检测（SpO2）。需要 **ESP-IDF v5.5 及以上**。

## 组件

| 组件 | 状态 | 功能 | 文档与测试 |
| --- | --- | --- | --- |
| [`esp_health`](./esp_health) | v0.1.0 | 面向乐鑫 SoC 的 PPG 心率与血氧算法 | [中文文档](./esp_health/README_CN.md) · [English](./esp_health/README.md) · [例程](./esp_health/examples) · [测试应用](./esp_health/test_apps) |

## 快速开始

### 通过 ESP Component Registry 添加

在 ESP-IDF 项目目录中运行：

```bash
idf.py add-dependency "espressif/esp_health"
```

支持的芯片、依赖和可选功能以 [`idf_component.yml`](./esp_health/idf_component.yml) 及组件文档为准。

### 从源码集成

将 `esp_health` 目录放入 ESP-IDF 项目的 `components/` 目录，然后按照该组件的 README 调用算法接口。模块说明、性能指标与硬件例程请参阅：

- [`esp_health` 中文文档](./esp_health/README_CN.md)
- [`heart_rate_detect` 例程](./esp_health/examples/heart_rate_detect)
- [`spo2_detect` 例程](./esp_health/examples/spo2_detect)
- [`test_apps`](./esp_health/test_apps)

## 仓库结构

```text
esp-health/
├── esp_health/   # 健康算法组件、文档、例程与测试
└── tools/        # 仓库测试与 CI 工具
```

后续健康相关组件将以独立顶层目录加入；每个组件自行维护源码、元数据、文档和测试，仓库根目录用于组件发现与导航。

## 反馈与贡献

- 遇到问题或有功能建议，请提交 [GitHub Issue](https://github.com/espressif/esp-health/issues)。
- 欢迎贡献算法、例程、测试及文档改进。
- 新组件应使用独立顶层目录，并同步更新根目录的中英文组件列表。
