# MAX30102 configuration

- [中文版本](./README_CN.md)

When attaching this module to another board, change the settings below to match the wiring, then re-run `idf.py bmgr -b <board_name> -a components/amend/max30102`.

| What to change | Where | Default in this example |
|----------------|--------|-------------------------|
| INT pin | `gpio_max30102_int.config.pin` in `max30102_sensor.yaml` **and** `MAX30102_INT_GPIO` in `esp_max30102.h` (keep them the same) | GPIO21 |
| I2C bus name | Peripheral name bound to the device in `max30102_sensor.yaml`; must match an I2C peripheral already on the target board | `i2c_master` |
| I2C address | `i2c_addr` in `max30102_sensor.yaml` | 0x57 |
| SDA / SCL | Use the target board's I2C pins. For the bundled empty board `esp32_s3_devkit_max30102`, edit that board's `board_peripherals.yaml` | GPIO19 / GPIO20 |

The target board must already provide an I2C master peripheral. This amend only adds the INT GPIO and the custom `max30102_ppg` device.

Deinit writes `MODE_CONFIG` SHDN (LEDs and analog off) before `i2c_master_bus_rm_device`. If shutdown or bus remove fails, the handle is kept so the caller can retry.

A short FIFO batch is not padded to the requested length. Wear detection and the application use the real sample count so IR average is not diluted.
