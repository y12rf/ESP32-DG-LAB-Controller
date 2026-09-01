# ESP32 DG-LAB 串口 CLI 设计

**日期：** 2026-09-01
**状态：** 已确认，待实施

## 背景

项目当前通过 ESP32 自建 Wi-Fi 热点中的 Web UI 控制 DG-LAB V2/V3 设备。固件已经把 BLE 生命周期、输出调度、协议状态机、应用状态和日志拆分为独立模块，但串口仅输出启动与运行日志，不能主动查询状态或执行控制动作。

本功能在保留 Web UI 的同时增加一个面向人工调试和维护的串口 CLI。CLI 与 Web UI 共享现有状态和控制模块，不建立另一套 BLE 或输出状态机。

## 目标

- 通过串口完成现有 Web UI 的扫描、连接、断开、自动连接、强度、波形、输出、状态和日志操作。
- 提供紧凑的英文终端面板，以及 ANSI 原地刷新的实时 `watch` 视图。
- 扫描结果使用从 1 开始的编号选择，适合人工输入。
- 保持 Web UI、V2/V3 协议、100 ms 输出调度和断线恢复语义不变。
- 使用固定输入缓冲、原地解析和直接串口输出，不增加运行时依赖或 FreeRTOS 任务。

## 非目标

- 不用 CLI 替代或移除 Wi-Fi Web UI。
- 不提供面向脚本的稳定机器协议、JSON 行协议或远程 shell。
- 不增加命令历史、自动补全、光标移动、权限系统或持久配置。
- 不修改 DG-LAB V2/V3 协议编码、波形表、强度状态机或恢复策略。
- 不把现有 `AppLog` 的中文运行日志翻译为英文。
- 不抽取 Web UI 与 CLI 共用的通用控制门面；首版保持两个轻量适配层独立。

## 选定方案

采用“独立轻量 `SerialCli` + 可独立测试的 `CliParser`”。

`CliParser` 使用固定结构体描述命令，以定长字符数组和原地分词完成解析，不依赖 Arduino、BLE 或动态内存。`SerialCli` 负责串口行输入、命令执行、终端渲染和错误提示，并像现有 `WebUi` 一样直接调用 `BleManager`、`OutputController`、`AppState` 与 `AppLog`。

没有采用共享控制门面，因为这会要求重构当前稳定的 `WebUi`，超出首版串口调试入口所需范围。没有采用 ESP32 自调用 HTTP API，因为它会引入本机网络请求、额外缓冲和无意义的协议转换。

## 架构与职责

### `CliParser`

- 输入一条以空字符结尾、可原地修改的 ASCII 命令行。
- 仅接受小写命令和关键字；命令名、通道、动作及枚举参数不做大小写兼容。
- 使用空白分词，不支持引号、转义或带空格的参数。
- 将合法输入解析为固定 `CliCommand`；数值参数使用无符号十进制并检查溢出。
- 区分未知命令、参数数量错误和参数值错误，供 `SerialCli` 输出对应提示。
- 不读取应用状态，也不执行任何控制动作。

### `SerialCli`

- 持有 `AppState`、`AppLog`、`BleManager`、`OutputController` 的引用。
- 管理一个 96 字节输入数组；最多接收 95 个命令字符和结尾空字符。
- 普通模式下显示 `$ ` 提示符、回显可打印 ASCII、处理退格，并把 CR、LF 或 CRLF 视为一次提交。
- 执行解析后的命令并直接使用 `Serial.print()` / `Serial.println()` 输出；固定文本使用 Flash 字符串，状态面板不拼接完整动态 `String`。
- 管理普通模式和 `watch` 模式；不创建任务，不阻塞等待输入。
- CLI 自身生成的标题、字段、结果和错误全部使用英文。

### `AppLog`

`AppLog` 继续保存最近 10 条原始日志，并继续作为 Web 日志接口的数据源。新增一个简单的串口镜像开关：

- 普通模式下，`AppLog::add()` 与现在一样保存并立即打印日志。
- 进入 `watch` 时关闭即时串口镜像，避免异步日志破坏 ANSI 面板。
- 退出 `watch` 时恢复即时串口镜像。
- 镜像关闭期间日志仍写入环形缓冲，可在退出后通过 `logs` 查看。

### `main.cpp`

在现有全局模块旁构造一个 `SerialCli`。`setup()` 在现有串口、Wi-Fi、BLE 和 Web 初始化后启动 CLI。

主循环保持以下顺序：

1. 处理 BLE 事件。
2. 清理断开的 BLE client。
3. 执行一次到期的输出调度。
4. 非阻塞读取和处理串口 CLI。
5. 处理 HTTP client。
6. 执行自动扫描判断。
7. `delay(10)` 让出 CPU。

这样串口渲染不会先于到期的 100 ms V3 B0 输出。

## 串口与终端约定

- 固件串口和 PlatformIO `monitor_speed` 从 `19200` 统一改为 `115200`。
- 源码及终端输出使用 UTF-8，以显示 `─`、`█` 和 `░`。
- ANSI 能力是 `watch` 的使用前提；不增加无 ANSI 的降级渲染器。
- 普通命令以换行提交。空行不执行动作，只重新显示提示符。
- 超过 95 个字符的行会进入丢弃状态，直到收到换行；随后只输出一次 `Command too long.`。
- `watch` 每 500 ms 到期时原地重绘，不使用阻塞延时。
- `watch` 中不回显普通输入；只接受单独一行 `q` 退出。其他行输出 `Press q then Enter to exit` 后继续监视。
- 退出 `watch` 不停止波形输出，也不改变连接或自动连接状态。

将串口提升至 115200，目的是让包含 UTF-8 条形图的一次面板重绘通常只占十几毫秒的串口传输时间，避免 19200 baud 下约百毫秒的输出占用影响波形调度。

## 命令语法

首版提供以下命令，不提供别名：

```text
help
status
watch
scan
devices
connect <index>
disconnect
autoconnect <on|off>
output <start|stop>
wave <a|b|c>
strength <a|b> <add|sub|set> <value>
logs
```

### `help`

输出命令清单和参数格式，不输出长篇说明。

### `status`

读取一次当前状态并输出固定字段面板。V3 示例：

```text
$ status
DG-LAB Controller ────────────────────────
Device     47L121000
Version    3.0
Connected  yes
Ready      yes
A          26 / 200
B          18 / 200
Feedback   confirmed
Wave       B
Output     running
Auto       off
```

`Feedback` 使用 `confirmed`、`waiting` 或 `unconfirmed`。未连接时设备和版本显示 `-`，`Connected` 与 `Ready` 显示 `no`，输出显示 `stopped`。

V2 同时显示人类刻度和协议原始值：

```text
A          26 / 292  (raw 182 / 2047)
B          18 / 292  (raw 126 / 2047)
```

V2 人类刻度使用 `raw / 7` 的整数结果并限制到 `0..292`；原始反馈保持 `0..2047`。V3 直接显示 `0..200`，不重复显示 raw。

### `watch`

进入非阻塞实时面板：

```text
$ watch
DG-LAB Live ──────────────────────────────
A  26 / 200  ███░░░░░░░░░░░░░░░░░
B  18 / 200  ██░░░░░░░░░░░░░░░░░░
Wave B · sending · ready · confirmed

Press q then Enter to exit
```

- 条形图固定为 20 格。
- 填充格数按 `(value * 20 + maximum / 2) / maximum` 四舍五入，并限制为 `0..20`。
- V3 的 `maximum` 为 200；V2 使用换算后的人类刻度和 292。
- 断连时不退出监视，面板显示空条和 `disconnected`，以便观察自动重连。
- Web UI 或自动重连造成的状态变化在下一次 500 ms 刷新时呈现。
- 每次刷新只移动光标并覆盖固定面板区域，不连续向终端追加完整快照。

### `scan` 与 `devices`

`scan` 仅在未连接、未扫描且没有 client 清理待处理时执行现有 3 秒同步扫描。完成后打印发现数量。

现有 `BleManager::startBleScan()` 语义保持不变：自动连接开启时，扫描结束会尝试连接信号最强的设备；手动选择前应执行 `autoconnect off`。

`devices` 输出最近一次扫描结果，编号从 1 开始：

```text
#  Device       Version  RSSI  Address
1  47L121000    3.0      -48   aa:bb:cc:dd:ee:ff
2  D-LAB EST    2.0      -67   11:22:33:44:55:66
```

设备名称直接按当前值输出，不实现复杂列宽截断或终端宽度检测。

### `connect` 与 `disconnect`

`connect <index>` 验证编号存在于当前扫描结果后：

1. 调用 `OutputController::onManualConnectionAttempt()` 清除自动恢复发送意图。
2. 使用扫描项保存的地址、设备类型和地址身份调用 `BleManager::connectToDevice()`。
3. 成功后调用 `OutputController::onConnected(true)`。

`disconnect` 仅在当前已连接时调用 `BleManager::disconnectDevice()`。既有手动断开语义保持不变：取消发送意图，不在自动重连后恢复输出。

### `autoconnect`

`autoconnect on|off` 修改现有 `AppState::autoConnectEnabled` 并通过 `AppLog` 记录变化。该命令不立即触发扫描；自动扫描仍由主循环的既有定时逻辑执行。

### `output` 与 `wave`

- `output start` 要求设备已连接且 `linkReady=true`，然后调用 `OutputController::startSending()`。
- `output stop` 始终允许调用 `OutputController::stopSending()`，以便清除发送意图。
- `wave a|b|c` 调用 `OutputController::selectWave()`；与 Web UI 一样，未连接时也可以预先选择波形。

### `strength`

`strength` 要求设备已连接且链路就绪。输入值为非负十进制整数，并使用 Web UI 当前的人类控制刻度：V3 为 `0..200`，V2 为 `0..292`。

映射到现有方法值：

| Channel | `add` | `sub` | `set` |
|---|---:|---:|---:|
| A | 4 | 8 | 12 |
| B | 1 | 2 | 3 |

命令调用 `OutputController::adjustStrength()`。结果为 `Prepared` 时输出 `Strength command prepared.`，为 `Queued` 时输出 `Strength command queued.`，为 `Rejected` 时输出 BLE 失败。V2 继续由现有控制器把人类刻度乘以 7；CLI 不复制协议换算逻辑。

### `logs`

按 `AppLog::newest()` 的现有顺序打印最多 10 条日志。CLI 的标题和空列表提示使用英文，日志内容保持现有原文，因此可能包含中文。

## 数据流与状态一致性

```text
Serial bytes
    -> fixed line buffer
    -> CliParser
    -> CliCommand
    -> SerialCli dispatcher
       -> read AppState/AppLog
       -> call BleManager
       -> call OutputController
```

CLI 与 HTTP 动作都在 Arduino 主循环中执行，不会同时修改控制状态。BLE 回调仍只通过原有原子标志和固定事件队列把事件交还主循环。`status` 与 `watch` 读取共享的最新状态，不缓存另一份设备模型。

CLI 不直接编码 BLE 帧、不直接写 BLE 特性，也不直接维护 V3 强度序列号。所有协议行为继续由 `OutputController` 和 `DgLabControl` 负责。

## 错误处理

CLI 使用简短英文错误，不输出 JSON 或机器错误码。固定错误包括：

```text
Unknown command. Run 'help'.
Usage: strength <a|b> <add|sub|set> <value>
Device index out of range.
Controller is busy.
Device is not connected.
BLE link is not ready.
Strength must be between 0 and 200.
Strength must be between 0 and 292.
BLE operation failed.
Command too long.
```

规则：

- 未知命令与已知命令的错误参数分开提示。
- 参数错误不执行部分动作。
- 扫描、连接和强度操作失败不在 CLI 层自动重试。
- BLE 自动扫描、断线清理和同设备恢复继续使用现有策略。
- `watch` 不因断连、未确认反馈或 BLE 错误自动退出。

## 性能约束

- 输入缓冲固定为 96 字节；解析器不使用 `String`、`std::vector`、正则表达式或动态 JSON。
- 状态和监视面板逐字段打印，不构造完整动态输出字符串。
- `watch` 只按 500 ms 周期刷新，不在循环的每次迭代打印。
- 到期的波形发送在 CLI 处理前执行。
- 不为 CLI 增加任务、队列、互斥锁或后台定时器。
- 不为了少见终端差异增加 ANSI 能力探测或多套渲染器。
- CLI 不改变扫描结果容器、日志容量或 Web 轮询策略。

## 代码变更范围

计划新增：

- `src/CliParser.h`：命令枚举、固定解析结果和解析接口。
- `src/CliParser.cpp`：无 Arduino 依赖的原地解析实现。
- `src/SerialCli.h`：串口 CLI 状态与公开接口。
- `src/SerialCli.cpp`：行输入、命令分发、状态面板和 `watch` 渲染。
- `test/test_cli_parser/test_main.cpp`：Native 解析器测试。
- `test/serial_cli_contract_test.py`：固件接线和性能约束契约测试。

计划修改：

- `src/AppLog.h`、`src/AppLog.cpp`：增加串口镜像开关。
- `src/main.cpp`：构造、初始化和轮询 `SerialCli`，并改用 115200 baud。
- `platformio.ini`：将 `monitor_speed` 改为 115200，并让 Native 环境编译 `CliParser.cpp`。
- `.github/workflows/platformio.yml`：在 PlatformIO 验证前运行串口 CLI 契约测试。
- `README.md`：记录 115200 串口速率、命令清单和人工使用示例。

不修改：

- `lib/DgLabControl` 协议库。
- `src/BleManager.*` 的 BLE profile 和生命周期实现。
- `src/OutputController.*` 的强度、波形和恢复语义。
- `src/WebUi.*`、`src/WebAssets.*` 的 HTTP API 和页面行为。
- 内置波形表。

## 自动化验证

### Native 解析器测试

Native Unity 测试覆盖：

- 每条无参数命令。
- `connect` 的合法编号、零、非数字和整数溢出。
- `autoconnect`、`output` 和 `wave` 的合法及非法枚举。
- `strength` 两个通道、三个动作、边界数值、负数、非数字和溢出。
- 缺少参数、额外参数、未知命令和大写命令。
- 前后空白和多个连续空白。

### Python 契约测试

`test/serial_cli_contract_test.py` 检查：

- 输入缓冲容量固定为 96 字节。
- `main.cpp` 的输出调度位于 CLI 和 HTTP 处理之前。
- CLI 命令通过现有 `BleManager` 和 `OutputController` 执行。
- `watch` 周期为 500 ms 且没有阻塞式刷新延时。
- `watch` 进入和退出时切换 `AppLog` 串口镜像。
- `Serial.begin()` 与 PlatformIO `monitor_speed` 都为 115200。
- CLI 不引入新的 Arduino 库、任务或动态 JSON。

### 完整自动验证

```powershell
python test/web_ui_contract_test.py
python test/serial_cli_contract_test.py
pio test -e native
pio run -e esp32dev
```

现有 Web UI 契约和协议状态机测试必须继续通过。固件构建需记录 Flash 与 RAM 用量，以确认 CLI 增量适合当前 ESP32 分区和内存预算。

## 真机验收

分别用 DG-LAB V2 和 V3 验证：

- 115200 baud 启动输出、提示符、回显、退格及 CR/LF/CRLF。
- 自动连接关闭后的扫描、列表和编号连接。
- 自动连接开启时扫描后连接信号最强设备。
- 手动断开不恢复输出，意外断线仍遵循既有同设备恢复策略。
- V3 `0..200` 状态与条形图。
- V2 `0..292` 人类刻度、`0..2047` raw 值与条形图。
- A/B 的 `add`、`sub`、`set`，包括 V3 `prepared`、`queued`、确认和超时状态。
- 波形 A/B/C 选择以及输出开始、停止。
- `watch` 运行时断连、自动重连、Web UI 控制和 `q` 退出。
- `watch` 期间日志仍被保存，退出后 `logs` 能显示最近 10 条。
- 持续发送时没有因串口刷新出现明显输出停顿、BLE 断连或写入失败。

## 成功标准

- 用户可以只通过串口完成 Web UI 已有的全部控制动作。
- `status` 和 `watch` 按确认的英文终端样式显示，V2 同时提供人类刻度与 raw 值。
- CLI 与 Web UI 可以在同一次运行中交替操作，状态保持一致。
- `watch` 非阻塞刷新，退出不改变设备输出状态。
- 现有 Web、协议和固件构建验证全部通过。
- 实现使用固定小缓冲、无新增依赖和无新增任务，符合 ESP32 性能约束。
