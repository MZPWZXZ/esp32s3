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
- 修复 OneNET Studio 断开问题：数据上报改用物模型主题 `$sys/{pid}/{dev}/thing/property/post` + OneJSON 格式（属性标识可配置 `MQTT_DRIVER_PROPERTY_ID`），订阅 `thing/property/post_reply` 与 `thing/service/property/set`（属性设置自动回复 `set_reply`），替换旧的 `$dp` 数据流主题。驱动内部自行处理订阅/上报/命令下发回复，`mqtt_driver_start()` 改为无参调用，移除外部事件回调接口。`task.c` 增加订阅 `cmd/request/+`，收到平台命令后自动回复到 `cmd/response/{cmdid}`；同时订阅数据上报响应主题 `dp/post/json/+`。`mqtt_driver` 增加产品 ID/设备名称/设备密钥/token 有效期配置，实现 OneNET token 生成（HMAC-SHA1，version=2018-10-31）与 CONNECT 三要素认证；`task.c` 增加 SNTP 时间同步；Broker URI 改为 `mqtt://studio-mqtt.heclouds.com:1883`；MQTT 任务改为在 Wi-Fi 连接后启动（`app_main` 中 `example_connect()` 之后调用 `mqtt_task_start()`）。
- 启用 MQTT 业务任务：`task_start_all()` 中调用 `mqtt_task_start()`（连接 `mqtts://test.mosquitto.org:8886`，TLS 证书包校验）。
- 组件重构：`components/` 目录更名为 `drivers/`，组件重命名为 `mqtt_driver` / `uart_driver`（根 `CMakeLists.txt` 增加 `EXTRA_COMPONENT_DIRS=drivers`）。
- 驱动与业务分离：两个组件只保留驱动能力（初始化/发送/接收回调、发布/订阅/事件分发），业务逻辑移至 `main`（UART 定时发 0xAA、MQTT 事件处理待启用）。
- 应用不再强制依赖网络：`uart_driver_start()` 先于 Wi-Fi 连接执行，Wi-Fi 失败仅告警不崩溃。
- 为 `components/uart_app` 新增发送函数 `uart_app_send()`，并增加定时发送任务：每 1s 通过 UART 发送 0xAA（间隔与开关可通过 menuconfig 配置）。
- 新增 `components/uart_app` 组件：实现 UART 通信（默认 GPIO1=RX、GPIO2=TX，可通过 menuconfig 修改），含 `CONFIG_UART_APP_ENABLED` 开关，并在 `app_main` 中调用 `uart_app_start()`。
- 为 `components/mqtt_app` 的代码补充中文 Doxygen 风格注释（@brief/@param/@return），无参数或返回值时使用 None。
- 将 MQTT 相关代码从 main 组件拆分为独立组件 `components/mqtt_app`，并新增 `CONFIG_MQTT_APP_ENABLED` 开关。
- 工程名由 `mqtt` 改为 `esp32s3`（根目录 `CMakeLists.txt` 的 `project()`）。
- 新增本文件，建立“变更记录 + 变更后自动提交并推送”的约定。
