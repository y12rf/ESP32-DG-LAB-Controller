# BLE Profile / Lifecycle 对齐报告

## 官方依据

- `docs/coyote/v2/README.md:3-8` 定义 V2 的 0x180B 服务必须提供 0x1504、0x1505、0x1506；0x1504 为读/写/通知，另外两条为读/写。`1504` 的读取仍为可选行为，连接可用性则要求三条特性都存在并具备写能力。
- `docs/coyote/v3/README.md:3-17` 定义 V3 的 0x180C/0x150A 写入和 0x150B 通知；`docs/coyote/v3/README.md:82-117` 规定 BF 为 7 字节、每次重连都要重新发送且写入后无返回值。
- 官方 Web Bluetooth demo 的 `docs/Web Bluetooth/bluetooth.html:74-79` 保留 `D-LAB` / `47` 前缀及当前 UUID；`209-240` 展示按前缀扫描、GATT 连接、获取服务；`251-259` 展示断开并清空连接对象。
- `docs/superpowers/specs/2026-08-30-non-security-control-fixes-design.md:165-201` 规定 unexpected disconnect 的 loop-owned 清理、固定事件队列、回调只更新链路标志/入队，以及 characteristic 指针先置空再删除 BLEClient。

## 本次实现

1. `BleManager::pollEvent()` 先消费队列；队列为空且 `deviceConnected=true`、`bleLinkAlive=false` 时合成一次 `Disconnected`，由 `handleDisconnectEvent()` 清除连接状态，避免断连事件丢失导致假连接。`takeDroppedEventCount()` 使用原子 exchange，`main.cpp` 每轮只记录一次丢弃数。
2. V2 连接要求 1504/1505/1506 全部存在且各自可 write 或 write-no-response；1504 仍只在可读时读取。所有 V2 写入通过 `writeBytes()`，response 参数由 `canWrite()` 决定。
3. V3 150A 的 BF、B0 均通过 `writeBytes()`，固定 BF 数组为 `{0xBF, 200, 200, 128, 0, 128, 0}`，并在 `linkReady` 设置前发送。NimBLE 返回值会决定 BF 是否成功；Classic API 的 `writeValue()` 返回 void，只能将调用完成视作成功，该限制已保留在代码注释。
4. cleanup 在删除 client 前清空全部 characteristic 指针，并同步 `bleLinkAlive=false`。`connectToDevice()` 现在强制接收 `const DeviceIdentity&`，不再有默认地址类型。
5. 手动连接在调用 BLE connect 前清除旧 resume identity/desired state；V2 wave、V2 strength、V3 B0 的写失败均调用 `handleTransportFailure()`，清 link-ready、保持非 manual 并请求断开，交给 unexpected-disconnect 恢复路径。

## Classic API 限制

当前 ESP32 Arduino BLE Classic 头文件中的 `BLERemoteCharacteristic::writeValue(uint8_t*, size_t, bool)` 返回 `void`，因此无法同步确认传输结果；实现只在 Classic 分支将“调用完成”作为成功。统一 helper 仍按 characteristic 能力选择 response/no-response。带 bool 返回的 NimBLE 分支直接传播 false，BF 失败会走连接失败的 deferred cleanup。

## 源级检查

- `rg 'new uint8_t|std::vector<uint8_t> bf' src/BleManager.cpp`：无匹配。
- `rg 'DeviceIdentity\\*|identity = nullptr|identity \\?' src/BleManager.h src/BleManager.cpp`：无匹配；所有 connect 调用均传入扫描记录的 identity。
- `rg 'D-LAB|devicePrefix_3_0 = "47"' src/BleManager.cpp`：前缀仍为官方 `D-LAB` / `47`，未硬编码完整设备名。
- `rg 'writeValue\\(' src/BleManager.cpp`：仅有统一 `writeBytes()` 的两条 Classic/NimBLE API 分支。
- 未引入 host BLE mock、BLE 抽象层、任务、锁或新队列。

## 验证

- `C:\Users\h\.platformio\penv\Scripts\pio.exe run -e esp32dev`：`SUCCESS`，RAM 17.9%，Flash 50.8%。
- `C:\Users\h\.platformio\penv\Scripts\pio.exe test -e native`：阻塞于当前机器未安装 host C/C++ 编译器（`gcc` / `g++` not recognized），未能编译 native 测试；不是本次源代码测试失败。
