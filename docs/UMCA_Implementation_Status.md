# UMCA implementation status

本次实现按开发计划的 P0–P3 顺序落地：

| Gate | 状态 | 说明 |
|------|------|------|
| A Codec | 完成 | 固定帧向量、CRC32C、Topic ID、最大 Payload 和错误路径均有主机测试 |
| B Core | 完成 | 多 Context、静态资源、Pub/Sub、TX/RX Sequence 和 Profile 裁剪 |
| C Discovery | 完成 | Loopback 多实例、ANNOUNCE/HEARTBEAT/TEARDOWN、节点状态和故障注入 |
| D Goldfish | 接入骨架 | openvela PAL、CMake/Kconfig/linkfile 和 ai_agent provider 已提供；尚未在当前 Goldfish 配置上运行完整固件 |
| E Agent 闭环 | 适配器骨架 | `sensor_query`/`device_control` provider 使用实际 `tool_registry_register_provider()` API；仍需由应用创建运行中的 UMCA Context/PHY 并驱动服务任务 |

主机验证命令见仓库根 README 和 `umca/README.md`。已验证
`MINIMAL`、`DISCOVERY`、`CONTEST` 三种 Profile 的构建；DISCOVERY 主机测试在
AddressSanitizer/UndefinedBehaviorSanitizer（关闭 LeakSanitizer 的 ptrace 检测）下通过。

尚未实现的协议能力保持关闭：分段、RPC、ACK/ERROR、QoS-1/2/3、网关、
多路径、安全和动态内存。
