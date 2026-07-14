# MAX30102 配置说明

- [English Version](./README.md)

接到其他开发板时，按实际接线修改下列配置，然后重新执行 `idf.py bmgr -b <board_name> -a components/amend/max30102`。

| 需要改什么 | 改哪里 | 本示例默认值 |
|------------|--------|--------------|
| INT 脚 | `max30102_sensor.yaml` 中 `gpio_max30102_int.config.pin`，**同时**改 `esp_max30102.h` 中 `MAX30102_INT_GPIO`（两处须一致） | GPIO21 |
| I2C 总线名 | `max30102_sensor.yaml` 里设备绑定的外设名，须与目标板上已有 I2C 外设同名 | `i2c_master` |
| I2C 地址 | `max30102_sensor.yaml` 的 `i2c_addr` | 0x57 |
| SDA / SCL | 使用目标板自带的 I2C 脚。若仍用本仓库空板 `esp32_s3_devkit_max30102`，改该板 `board_peripherals.yaml` | GPIO19 / GPIO20 |

目标板须已提供可用的 I2C master 外设。本 amend 只追加 INT GPIO 与 `max30102_ppg` 自定义设备。

反初始化会先写 `MODE_CONFIG` 的 SHDN（关闭 LED 与模拟前端），再调用 `i2c_master_bus_rm_device`。若关断或拆除总线设备失败，句柄会保留，调用方可重试。

FIFO 短批不会填充到请求长度。佩戴检测与应用层使用真实采样数，避免 IR 均值被稀释。
