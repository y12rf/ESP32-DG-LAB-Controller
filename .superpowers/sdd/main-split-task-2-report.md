# Main Split Task 2 Report

状态：DONE_WITH_CONCERNS

## 修改文件

- `src/BleManager.h`
- `src/BleManager.cpp`
- `src/main.cpp`

`BleManager` 现在持有 BLE 客户端、扫描/连接/通知回调、事件队列、扫描结果和 V2/V3 特征句柄；主模块通过管理器完成扫描、连接、断连清理、事件轮询和底层写入。连接状态收尾仍由 `main.cpp` 中的临时输出收尾函数完成。

## 提交

提交 SHA：`2df9530`

提交信息：`refactor: extract BLE manager`

## 验证命令与结果

```powershell
pio test -e native
pio run -e esp32dev
```

本机 `pio` 命令不可用，因此按任务给定路径调用 PlatformIO：

```powershell
& 'C:\Users\h\.platformio\penv\Scripts\platformio.exe' test -e native
```

结果：默认 PlatformIO core 因 `C:\Users\h\.platformio\platforms.lock` 权限错误无法启动。

随后使用项目 `.piohome-verify` core 重试 Native：构建阶段因 Windows 环境没有 `gcc`/`g++` 失败。

ESP32 构建使用项目 `.piohome` core 重试：Arduino framework 下载停留在 `Downloading 0%`，等待后终止；使用已存在全局 packages 重试时因 `C:\Users\h\.platformio\packages.lock` 权限错误失败。未为绕过环境问题修改代码。

## 自审

- BLE UUID、设备前缀、扫描去重、连接服务重试、MTU 517、BF 字节、16 项队列、事件丢弃计数、通知过滤和断连处理均已迁移到 `BleManager`。
- `startBleScan()`/`handleAutoScan()` 仅在同步自动连接成功时返回 `true`。
- `handleDisconnectEvent()` 保持日志、连接状态、链路状态、清理标志的顺序；清理后的强度和恢复策略仍由主模块临时收尾函数处理。
- V2/V3 底层写入调用已改由管理器完成；Web 路由、强度逻辑和波形逻辑仍留在 `main.cpp`。
- `git diff --check` 无错误；仅暂存并提交了本任务的三个源文件，`.gitignore`、`.piohome*`、`.pushgit`、`.superpowers` 未提交。
- 未新增测试，符合简报要求。

## 疑虑

- 本机无法完成规定的 Native 编译和 ESP32 固件编译，因此未取得本地 `SUCCESS` 或尺寸数据；需依赖 GitHub Actions 做最终编译验收。
- 无硬件 BLE 实机验证。

## Task 2 回归修复

- 手动断开时在临时输出收尾中同步 `ResumePolicy::desiredSending` 为 `false`，恢复手动断开后不自动恢复输出的原有语义。
- 新增轻量 `BleManager::hasV2StrengthCharacteristic() const`；2.0 强度高层在缺失 PWM_AB2 特性时只记录“PWM_AB2 特性获取失败”，原始写入方法不重复记录，保留传输失败时的“设置2.0强度失败”日志语义。

验证：`pio` 不在 PATH；直接调用 PlatformIO 后，Native 测试因本机没有 `gcc`/`g++` 失败。ESP32 固件构建成功（RAM 17.9%，Flash 50.8%）。静态检查确认改动仅涉及上述两处行为修复及本报告。
