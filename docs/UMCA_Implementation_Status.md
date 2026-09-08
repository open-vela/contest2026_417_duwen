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

硬件阶段（2026-09-06）：目标板确认是 GD32F470V-START，MCU 为 GD32F470VKT6。
UART 静态字节流 PHY、openvela 文件描述符桥接和通用 `umca_gd32` NSH 应用已实现，
主机 4/4 测试通过。2026-09-08 切换 UART3 并全量重编译后，GD32 固件已成功链接：
Flash 244,640 B，SRAM region 15,920 B。原理图复核后硬件接口已更新为 UART3、
PC10/TX、PC11/RX、AF8、115200 8N1、
3.3 V TTL、`/dev/ttyS1`；
USART0/PB6/PB7 `/dev/ttyS0` 保留给 NSH，SDIO 因引脚冲突保持关闭。PA9 与 PD2
分别接入板载 USB VBUS 检测和电源控制网络，旧方案已废止。实板烧录、
UART 帧互操作和 GD32↔ESP32 透传仍待完成。
按原理图完成的扩展配置审计已在编译期禁用不适用 LQFP100 的 SPI5、
ENET、EXMC、TLI 和 DCI 旧映射；200 MHz Profile 下禁用需要精确 48 MHz
时钟域的 USB FS、SDIO 和 TRNG。板载 LED 修正为 PC6，低有效用户按键修正为
PA0 并使用双边沿 IRQ。
Linux 负责编译，Windows 负责 GD-Link/CMSIS-DAP 烧录和 GDB 调试；`.vscode/` 未修改。
同时已修复本地 NuttX GD32F4 早期串口初始化对空槽位的空指针解引用；该公共仓改动
与参赛仓代码分开维护，本地提交为 `c42f0d1da26`（未推送）；GD32 全量编译和
NuttX `checkpatch.sh` 已通过。

尚未实现的协议能力保持关闭：分段、RPC、ACK/ERROR、QoS-1/2/3、网关、多路径、
安全和动态内存。Goldfish Loopback 资源数据不得外推为 GD32 资源结论。
