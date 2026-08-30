# PlatformIO 项目结构迁移设计

## 目标

将现有 Arduino 单文件项目迁移为标准 PlatformIO 项目，使其可使用通用 ESP32 Dev Module（`esp32dev`）进行构建、上传和串口监视，同时保持现有 Wi-Fi、Web、BLE 和波形控制行为不变。

## 项目结构

迁移后新增 `platformio.ini` 和 `src/main.cpp`。原有 `ESP32-DG-LAB-Controller.ino` 的内容整体迁入 `src/main.cpp`，仅增加标准 C++ 编译所需的 `Arduino.h` 头文件。现有 `docs/`、`LICENSE` 和其他资料保持原位。

## PlatformIO 配置

默认环境固定使用官方 Espressif 32 平台 `6.13.0`、`esp32dev` 开发板和 Arduino 框架。串口监视器波特率设为代码当前使用的 `19200`。由于 BLE 和 Web 固件超过默认应用分区容量，使用 `huge_app.csv`（3 MB 应用区、无 OTA）分区方案。项目没有第三方库依赖，WiFi、WebServer 和 BLE 均由 Arduino-ESP32 框架提供。

## 配套调整

- `.gitignore` 忽略 PlatformIO 的 `.pio/` 和编辑器本地目录。
- `README.md` 的软件要求和安装步骤改为以 PlatformIO 为主，给出构建、上传和串口监视命令。
- 不拆分业务模块，不修改运行逻辑，不调整 Wi-Fi 或 BLE 参数。

## 验证

使用 `pio run` 完成一次 `esp32dev` 环境编译。若本机没有 PlatformIO，则先检查可用的 PlatformIO CLI 入口；只有在无法进行本地构建时，才明确记录未验证原因。
