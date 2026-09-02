# 血氧饱和度检测示例

- [English Version](./README.md)
- 例程难度：⭐⭐

## 例程简介

本示例基于 **MAX30102** PPG 传感器和 **esp-health**（`esp_health_spo2`），演示静止 / 低运动条件下的实时双通道血氧测量。示例通过 I2C 同步采集 **IR** 与 **Red** PPG，按 5 s 窗调用算法估算 SpO2，并在串口打印结果。

- 板级初始化由 [ESP Board Manager](https://github.com/espressif/esp-board-manager) 完成。
- 血氧估算由 `esp_health_spo2_open` / `esp_health_spo2_process` / `esp_health_spo2_reset` / `esp_health_spo2_close` 完成。
- 佩戴检测、上次有效值保持与离指后 `reset` 在**应用层**实现，库内不含运动伪迹抑制。
- 默认配置为 `ESP_HEALTH_SPO2_CFG_DEFAULT()`（MAX30102 类 660 nm / 880 nm），非医疗级。

### 典型场景

- 指夹式静止贴合 MAX30102，观察串口血氧日志。
- 作为 `esp_health_spo2` 的集成参考：采集任务 + 处理任务 + 接触会话 `reset`。

### 运行机制

```mermaid
graph LR
    MAX30102 --> Acq["采集任务"]
    Acq --> Ring["5 s 环形缓冲"]
    Ring --> Proc["处理任务"]
    Proc --> SpO2["esp_health_spo2_process"]
    SpO2 --> UART["串口日志"]
```

- 采集任务：从 MAX30102 读取 IR + Red 数据块（`CHUNK_SAMPLES = 32`），根据接触标志更新佩戴状态；离指或连续约 1 s FIFO/I2C 失败时清空会话、丢弃上次 SpO2，并标记需要 `reset`。FIFO overflow 视为时间断点：丢弃环形缓冲，重新攒满一窗。
- 处理任务：仅在环形缓冲有**新样本**时拷贝 5 s IR/Red 窗，在**同一任务**中先按需调用 `esp_health_spo2_reset()`，再调用 `esp_health_spo2_process`（句柄非线程安全）。若 `process()` 期间发生离指或 FIFO overflow，本次结果丢弃、不发布。仅在返回 `ESP_HEALTH_ERR_OK` 且 `spo2` 落在 70–100 时更新显示值；若本窗无效但 `quality_ratio` 仍可用，则沿用上次有效 SpO2。FIFO 停住时不会重复发布旧窗口。其他任务应通过 `ppg_spo2_get_measurement()` 读取（同一 mutex 下快照）。

## 环境配置

### 硬件要求

- **开发板**：默认以 ESP32-S3-DevKitC 为例，配合自定义板型 `esp32_s3_devkit_max30102`。
- **传感器**：MAX30102 PPG 模组。
- **接线**（参考板型 `esp32_s3_devkit_max30102`）：

| MAX30102 | ESP32-S3 GPIO |
|----------|---------------|
| SDA      | GPIO19        |
| SCL      | GPIO20        |
| INT      | GPIO21        |
| VIN / GND | 3V3 / GND   |

### 默认 IDF 分支

本例程在 IDF v6.x（CI：release/v6.1）上验证。请使用与 `espressif/esp_board_manager` 兼容的 IDF 版本。

### 预备知识

本例程使用自定义板型与 MAX30102 驱动扩展，需通过 `idf.py bmgr` 生成板级代码后再编译。IR 与 Red 必须同步采集，且为**正直流**原始 PPG。

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段，直接进入工程目录。若未配置，请在 ESP-IDF 根目录运行以下脚本完成环境设置，完整步骤请参阅 [《ESP-IDF 编程指南》](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)。

```bash
./install.sh
. ./export.sh
```

下面是简略步骤：

- 进入本例程工程目录：

```bash
cd $YOUR_ESP_HEALTH_PATH/examples/spo2_detect
```

本示例使用 [ESP Board Manager](https://github.com/espressif/esp-board-manager) 管理板级资源。推荐安装辅助工具 [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) 作为默认入口。

- 在已激活的 ESP-IDF Python 环境下安装（同一环境只需安装一次）：

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist  # 当提示需要更新时执行此命令
```

- 选择带 MAX30102 的自定义板型：

```bash
idf.py bmgr -b esp32_s3_devkit_max30102 -a components/amend/max30102
```

  首次执行 `idf.py bmgr` 时，组件会根据本工程 `main/idf_component.yml` 中声明的 `espressif/esp_board_manager` 依赖自动下载。生成代码位于 `components/gen_bmgr_codes/`。

该示例中的 MAX30102 模块可通过 `-a/--amend` 功能复用于其他基于 ESP Board Manager 适配的开发板。只需根据目标板的实际硬件连接调整部分配置，并在生成板级代码时指定该 amend 目录即可接入。`-a/--amend` 具体用法可参考 [Board Amend 文档](https://docs.espressif.com/projects/esp-board-manager/zh_CN/latest/create-board/amend.html#id3)。具体需要修改的配置请参考 [MAX30102 配置说明](components/amend/max30102/README_CN.md)。

> [!NOTE]
> 自定义开发板请参考 [创建开发板指南](https://docs.espressif.com/projects/esp-board-manager/zh_CN/latest/create-board/index.html)。
> `esp_board_manager` 更多信息请参考 [ESP_BOARD_MANAGER 入门指南](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md)。

`esp_health` 为本地组件，在 `main/idf_component.yml` 中通过 `override_path: "../../.."` 引入。

### 项目配置

本例程无额外功能选项，一般无需修改 menuconfig。公共配置在 `sdkconfig.defaults`；ESP32-S3 覆盖项在 `sdkconfig.defaults.esp32s3`（另有 `.esp32p4` / `.esp32s31`）。如需调整日志等级等，可执行：

```bash
idf.py menuconfig
```

> 配置完成后按 `s` 保存，然后按 `Esc` 退出。

### 编译与烧录

- 编译示例程序

```bash
idf.py build
```

- 烧录程序并运行 monitor 工具来查看串口输出（将 `PORT` 替换为实际串口号）：

```bash
idf.py -p PORT flash monitor
```

- 退出调试界面使用 `Ctrl-]`

## 如何使用例程

### 功能和用法

1. 烧录后，将手指**稳定贴合** MAX30102 光学窗口，覆盖红外与红光发光面。
2. 等待约 5 s 完成首窗采集。
3. 串口输出 `spo2` / `corr` / `q`；抬指后打印 `finger off`。
4. 离指后再上指时，处理任务会先 `esp_health_spo2_reset()` 再 `process`，避免沿用上一会话的周期性状态。

### 日志输出

正常流程依次为初始化、采集/处理任务启动、首窗缓冲与 SpO2 输出。以下为关键 log：

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

- `Collecting first 5s window...`：正在填充首个分析窗
- `spo2=NN%`：本窗有效血氧估计
- `finger off`：佩戴/接触检测判定离指
- `sensor lost`：连续 FIFO/I2C 失败；丢弃上次 SpO2，恢复后需重新攒满一窗

### 关键参数（`main/ppg_spo2.c`）

| 宏 / API | 取值 | 含义 |
|----------|-----:|------|
| `SAMPLE_RATE` | 100 | PPG 采样率（Hz），支持范围 16–1000 |
| `WINDOW_SEC` | 5 | 分析窗（s） |
| `CHUNK_SAMPLES` | 32 | 每次 FIFO 读取的样本数 |
| `SPO2_ACQ_FAIL_INVALIDATE` | 50 | 连续 FIFO 失败次数（约 1 s）后丢弃会话 |
| `cfg` | `ESP_HEALTH_SPO2_CFG_DEFAULT()` | MAX30102 类默认配置（示例中会覆盖门限） |
| SpO2 范围 | 70–100 | `esp_health_spo2` 有效 SpO2 范围 |

### 代码结构

```text
spo2_detect/
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32s3      # 另有 .esp32p4 / .esp32s31
├── README.md / README_CN.md
├── main/
│   ├── ppg_spo2.c / ppg_spo2.h # 应用入口与血氧流水线
│   ├── spo2_freshness.h        # 跳过旧窗口；I2C 丢失时丢弃会话
│   ├── spo2_meas_pub.h         # ppg_spo2_get_measurement 的 mutex 快照
│   ├── CMakeLists.txt
│   └── idf_component.yml       # esp_board_manager + 本地 esp_health override
└── components/
    ├── board_customer/         # 自定义板：esp32_s3_devkit_max30102
    ├── amend/max30102/         # MAX30102 驱动与板级扩展
    └── gen_bmgr_codes/         # idf.py bmgr 生成，勿手改
```

## 故障排除

### MAX30102 初始化失败或 FIFO 读取失败

若日志出现 I2C / soft reset 失败或 `FIFO read fail`，请确认 SDA=GPIO19、SCL=GPIO20、INT=GPIO21、供电为 3V3，以及传感器地址为 0x57。IR 与 Red 须同步且为正直流。

### `spo2` 为 0 或经常 finger off

请保持手指稳定覆盖光学窗口，避免漏光与体动。窗口过噪、非周期或 `R` / SpO2 越界时，库仍返回 `ESP_HEALTH_ERR_OK` 但 `spo2 = 0`。换波长或光路须重填 `esp_health_spo2_calib_t`。

### `idf.py bmgr` 找不到板型

请在例程目录执行 `idf.py bmgr -b esp32_s3_devkit_max30102 -a components/amend/max30102`。首次运行需能访问组件仓库以下载 `esp_board_manager`。

## 技术支持

请按照下面的链接获取技术支持：

- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-health/issues)

我们会尽快回复。
