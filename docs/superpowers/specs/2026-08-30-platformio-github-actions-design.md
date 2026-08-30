# PlatformIO GitHub Actions 自动编译设计

## 目标

为项目增加 GitHub Actions 持续集成。在 `main` 分支收到 push，或 pull request 的目标分支为 `main` 时，自动执行 PlatformIO 编译并以构建退出状态判断检查是否通过。

## 工作流

新增 `.github/workflows/platformio.yml`，使用 GitHub 托管的 `ubuntu-latest` runner。工作流允许在 GitHub 页面手动触发，但不会上传固件、创建 Release 或执行设备烧录。

工作流步骤为：检出仓库、配置明确版本的 Python、恢复 pip 与 PlatformIO 下载缓存、安装 PlatformIO Core、执行 `pio run`。项目继续使用 `platformio.ini` 中固定的 `platformio/espressif32@6.13.0`、`esp32dev` 和 `huge_app.csv` 配置。

## 权限与缓存

工作流只授予 `contents: read` 权限。缓存仅包含 `~/.cache/pip` 和 `~/.platformio/.cache`，缓存键包含操作系统和 `platformio.ini` 的哈希；配置变化时自动生成新缓存，同时允许使用同一操作系统的旧缓存作为下载加速起点。

## 失败处理

安装 PlatformIO 或执行 `pio run` 失败时，job 直接失败，不使用 `continue-on-error`。不增加重试、通知或自动提交逻辑。

## 验证

- 本地执行 `pio run`，确认当前项目仍可编译。
- 解析工作流 YAML，确认语法有效。
- 检查触发器只包含 `main` 的 push、目标为 `main` 的 pull request，以及手动触发。
- 检查工作流最终执行 `pio run`，且不包含 artifact、release 或 upload target。
