# UMCA implementation status

本次实现按开发计划的 P0–P3 顺序落地：

| Gate | 状态 | 说明 |
|------|------|------|
| A Codec | 完成 | 固定帧向量、CRC32C、Topic ID、最大 Payload 和错误路径均有主机测试 |
| B Core | 完成 | 多 Context、静态资源、Pub/Sub、TX/RX Sequence 和 Profile 裁剪 |
| C Discovery | 完成 | Loopback 多实例、ANNOUNCE/HEARTBEAT/TEARDOWN、节点状态和故障注入 |
| D Goldfish | 完成（Loopback） | openvela PAL、三实例 Loopback PHY、独立服务任务和三节点 Sensor→Agent→Actuator 闭环已在当前 Goldfish 配置运行 |
| E Agent 闭环 | 完成 MVP | `sensor_query`/`device_control` 通过实际 `tool_registry` provider 运行；Tool 等待匹配来源、`request_id` 和 `result==0` 的 FanState |

主机验证命令见仓库根 README 和 `umca/README.md`。已验证
`MINIMAL`、`DISCOVERY`、`CONTEST` 三种 Profile 的构建，当前 CTest 为 3/3 通过；
DISCOVERY 主机测试在 AddressSanitizer/UndefinedBehaviorSanitizer（关闭 LeakSanitizer
的 ptrace 检测）下通过。Goldfish 运行时可使用 `umca_agent tools`、
`umca_agent sensor_query` 和 `umca_agent device_control on|off` 复核 provider 接入。

尚未实现的协议能力保持关闭：分段、RPC、ACK/ERROR、QoS-1/2/3、网关、
多路径、安全、UART/GD32/ESP32 硬件互操作和动态内存。Goldfish Loopback 资源数据
不得外推为 GD32 资源结论。
