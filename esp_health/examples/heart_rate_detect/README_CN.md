# 心率检测示例

- [English Version](./README.md)
- 例程难度：⭐⭐

## 例程简介

本示例基于 **MAX30102** PPG 传感器和 **esp-health**（`esp_health_hr`），演示静止 / 低运动条件下的实时心率测量。示例通过 I2C 采集 RED 通道 PPG，按滑动窗口调用算法估算 BPM，并在串口打印结果。

- 板级初始化由 [ESP Board Manager](https://github.com/espressif/esp-board-manager) 完成。
- 心率估算由 `esp_health_hr_open` / `esp_health_hr_process` / `esp_health_hr_close` 完成。
- 佩戴检测、EMA 平滑与失败窗口保持在**应用层**实现，库内不含运动伪迹抑制。

### 典型场景

- 指端或腕部静止贴合 MAX30102，观察串口心率日志。
- 作为 `esp_health_hr` 的集成参考：采集任务 + 算法任务 + 轻量后处理。

### 运行机制

```mermaid
graph LR
    MAX30102 --> Data["采集任务"]
    Data --> Queue["FreeRTOS 队列"]
    Queue --> Algo["算法任务"]
    Algo --> HR["esp_health_hr_process"]
    HR --> UART["串口日志"]
```

- 采集任务：每秒从 MAX30102 读取一组 RED PPG（`SAMPLE_RATE = 100`），将采样与佩戴 / FIFO-gap 放在同一条队列消息里（不共享 `detect_flag`）。FIFO overflow 视为时间断点：丢弃队列与分析环，重新攒窗。
- 算法任务：写入最长约 15 s 的环形缓冲。满 **4 s** 后第一次调用 `esp_health_hr_process`，之后每 1 s 一次，分析窗封顶 **8 s**（头文件建议 8–15 s）。仅在返回 `ESP_HEALTH_ERR_OK` 且 `avg_bpm` 落在 40–200 时更新显示值；否则短暂保持上次有效 BPM。倍频校正在库内完成。

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

本例程使用自定义板型与 MAX30102 驱动扩展，需通过 `idf.py bmgr` 生成板级代码后再编译。

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
cd $YOUR_ESP_HEALTH_PATH/examples/heart_rate_detect
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

1. 烧录后，将手指**稳定贴合** MAX30102 光学窗口。
2. 等待约数秒完成预热与窗口填充。
3. 串口输出有效 BPM；抬指后打印 `finger off`。
4. 体动、接触不良或漏光时可能进入 `measuring` / `hold`，或 BPM 跳变。

### 日志输出

正常流程以 `[ 1 ]`～`[ 4 ]` 标出初始化，随后周期性打印心率。以下为关键 log：

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

- `-- (measuring)`：预热、窗口过短或信号未稳定
- `NN BPM`：有效窗口得到的心率估计（示例侧已做 EMA 平滑）
- `hold`：本窗无有效读数，短暂保持上次 BPM
- `finger off`：佩戴/接触检测判定离指

### 关键参数（`main/ppg_heart_rate.c`）

| 宏 | 取值 | 含义 |
|----|-----:|------|
| `SAMPLE_RATE` | 100 | PPG 采样率（Hz），支持范围 16–1000 |
| `RING_SEC` | 15 | 环形缓冲时长（s） |
| `ANALYSIS_SEC` | 8 | 分析窗（s），封顶 |
| `HR_EXAMPLE_MIN_WINDOW_SEC` | 4 | 环形缓冲攒满这么多秒后第一次 `process` |
| `UPDATE_MS` | 1000 | 心率更新周期（ms） |
| BPM 范围 | 40–200 | `esp_health_hr` 有效 BPM 范围 |

### 代码结构

```text
heart_rate_detect/
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32s3      # 另有 .esp32p4 / .esp32s31
├── README.md / README_CN.md
├── main/
│   ├── ppg_heart_rate.c        # 应用入口与心率流水线
│   ├── hr_example_win.h        # 4 s 起算、1 s 步进、8 s 封顶
│   ├── hr_ppg_msg.h            # 队列消息类型：采样 / 离指 / FIFO gap
│   ├── CMakeLists.txt
│   └── idf_component.yml       # esp_board_manager + 本地 esp_health override
└── components/
    ├── board_customer/         # 自定义板：esp32_s3_devkit_max30102
    ├── amend/max30102/         # MAX30102 驱动与板级扩展
    └── gen_bmgr_codes/         # idf.py bmgr 生成，勿手改
```

## 故障排除

### MAX30102 初始化失败

若日志出现 I2C / soft reset 失败，请确认 SDA=GPIO19、SCL=GPIO20、INT=GPIO21、供电为 3V3，以及传感器地址为 0x57。

### 一直显示 measuring 或 BPM 跳变

请保持手指稳定覆盖光学窗口，避免漏光与体动。`esp_health_hr` 无运动伪迹抑制；走路 / 跑步等场景需在应用层叠加 ACC 门控。

### `idf.py bmgr` 找不到板型

请在例程目录执行 `idf.py bmgr -b esp32_s3_devkit_max30102 -a components/amend/max30102`。首次运行需能访问组件仓库以下载 `esp_board_manager`。

## 技术支持

请按照下面的链接获取技术支持：

- 问题反馈与功能需求，请创建 [GitHub issue](https://github.com/espressif/esp-health/issues)

我们会尽快回复。
