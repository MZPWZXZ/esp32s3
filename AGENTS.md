# AGENTS.md

本文件是项目的代理工作约定与变更记录（Windows 下与 Agent.md 同名，不区分大小写）。

## 工作约定

1. 每次对工程做出变更后，必须在下方“变更记录”中追加一条，包含日期和变更说明。
2. 追加记录后，立即将本次变更提交到 Git 并推送到远程：
   - `git add -A`
   - `git commit -m "..."`（提交信息简洁描述本次变更）
   - `git push`
3. 若一次工作包含多个独立变更，尽量按变更点分别提交。

## 变更记录

### 2026-08-30
- 新增 `components/uart_app` 组件：实现 UART 通信（默认 GPIO1=RX、GPIO2=TX，可通过 menuconfig 修改），含 `CONFIG_UART_APP_ENABLED` 开关，并在 `app_main` 中调用 `uart_app_start()`。
- 为 `components/mqtt_app` 的代码补充中文 Doxygen 风格注释（@brief/@param/@return），无参数或返回值时使用 None。
- 将 MQTT 相关代码从 main 组件拆分为独立组件 `components/mqtt_app`，并新增 `CONFIG_MQTT_APP_ENABLED` 开关。
- 工程名由 `mqtt` 改为 `esp32s3`（根目录 `CMakeLists.txt` 的 `project()`）。
- 新增本文件，建立“变更记录 + 变更后自动提交并推送”的约定。
