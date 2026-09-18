# UMCA implementation status

本次实现按开发计划的 P0–P3 顺序落地：

| Gate | 状态 | 说明 |
|------|------|------|
| A Codec | 完成 | 固定帧向量、CRC32C、Topic ID、最大 Payload 和错误路径均有主机测试 |
| B Core | 完成 | 多 Context、静态资源、Pub/Sub、TX/RX Sequence 和 Profile 裁剪 |
| C Discovery | 完成 | Loopback 多实例、ANNOUNCE/HEARTBEAT/TEARDOWN、节点状态和故障注入 |
| D Goldfish | 完成（Loopback） | openvela PAL、三实例 Loopback PHY、独立服务任务和三节点 Sensor→Agent→Actuator 闭环已在当前 Goldfish 配置运行 |
| E Agent 闭环 | 完成 MVP并通过实板业务验收 | Goldfish Tool闭环及 GD32配合 Windows三节点模拟的 Discovery、查询、手动/自动控制、Mock LLM和动作执行均通过 |
| F ESP32-S3网关 | 实体网关验收通过 | 实体ESP32-S3完成串口路由和MiMo云端API闭环；Sensor最终由Windows串口模拟 |

主机验证命令见仓库根 README 和 `umca/README.md`。已验证
`MINIMAL`、`DISCOVERY`、`CONTEST` 三种 Profile 的构建，DISCOVERY CTest为5/5通过；
DISCOVERY 主机测试在 AddressSanitizer/UndefinedBehaviorSanitizer（关闭 LeakSanitizer
的 ptrace 检测）下通过。Goldfish 运行时可使用 `umca_agent tools`、
`umca_agent sensor_query` 和 `umca_agent device_control on|off` 复核 provider 接入。

硬件阶段（2026-09-06）：目标板确认是 GD32F470V-START，MCU 为 GD32F470VKT6。
UART 静态字节流 PHY、openvela 文件描述符桥接和通用 `umca_gd32` NSH 应用已实现，
主机 4/4 测试通过。2026-09-09 修复远端 Topic 有效标志和节点查询逐字段复制后，
GD32 固件已成功链接：Flash 244,656 B，SRAM region 15,920 B。原理图复核后硬件接口已更新为 UART3、
PC10/TX、PC11/RX、AF8、115200 8N1、
3.3 V TTL、`/dev/ttyS1`；
USART0/PB6/PB7 `/dev/ttyS0` 保留给 NSH，SDIO 因引脚冲突保持关闭。PA9 与 PD2
分别接入板载 USB VBUS 检测和电源控制网络，旧方案已废止。2026-09-09 已完成
GD32 UART3 实板双向收发和 UMCA 协议验收：ANNOUNCE、HEARTBEAT、DATA、CRC32C、
Sequence、半帧、粘包、COM 重开、新 Boot ID 会话和离线转换均通过。基础冒烟测试
错误码为 0 且无 HardFault；完整负向测试结束后的粘滞错误为预期的
`UMCA_ERR_CRC(-7)`。在该阶段，真实 GD32↔ESP32硬件互操作和长期连续运行测试尚未
完成；其中硬件互操作已于2026-09-18通过最终验收，长期连续运行仍作为独立可靠性项目。
Windows 分阶段验证工具已加入 `scripts/windows/`：阶段一工具生成并校验原始 UMCA
UART 测试流；阶段二 COM↔TCP 网桥保持纯字节透明，采用固定容量双向缓冲，并在 TCP
断线时销毁旧 Socket、清空缓冲、丢弃断线期间 UART 输入。原网桥和新增三节点模拟器
的工具主机测试共14/14通过。
Windows COM7 第一阶段实测已通过：`rx_frames=13`、`rx_crc_errors=1`、
`rx_duplicates=1`、`rx_stale=1`、`rx_unsubscribed=6`、`rx_semantic_errors=0`，新 Boot ID
会话建立并在 15 秒无心跳后正常离线；`node_online_events=1`、`node_offline_events=1`、
`capacity_errors=0`。
远端 Topic 修复已通过 DISCOVERY/CONTEST 主机回归和 GD32 实机回归，实机确认
`g_service.context.nodes[0].topics[0].used == true`。
2026-09-14，COM7↔TCP 第二阶段基线通过：TCP→UART 493/493 字节，UART→TCP
读取 245 字节、转发 98 字节、断线丢弃 147 字节；连接/断开各 2 次，无缓冲溢出和
串口清理失败。GD32 `last_error=0`、`rx_frames=9`、`rx_unsubscribed=5`，全部错误
统计为 0；两次 TCP 接收均为 `invalid_frames=0, discarded_bytes=0`，最终节点 OFFLINE，
无 HardFault。
阶段二 Windows 测试节点固定 DevID 为 `0x3300000000000001`；初始 Boot ID 为
`0x33000001`，重连后为 `0x33000002`。两者是独立字段，不进行拼接。
固定缓冲溢出及非空缓冲断线清空专项测试随后通过，阶段二 Windows 网桥功能验收
完成。极小缓冲断线时，已进入 UART 物理发送过程的字节不可撤回，可能产生一次预期
CRC 错误，接收端可重新同步；未发现其他已确认的正式网桥代码问题。Windows GBK 中文
输出异常仅影响日志显示。当时尚未完成的真实 ESP32替换已于2026-09-18通过最终验收，
长期连续运行仍作为独立可靠性项目。最终器件已确认为
ESP32-S3-N16R8，产品角色由纯透明桥调整为三串口固定路由网关和本地 LLM UMCA节点。
GD32 UART仍为单 PHY，ESP32按静态 DevID出口转发 Sensor/Actuator帧；LLM请求/响应
使用 `/llm/chat/request`和 `/llm/chat/response`两个 DATA Topic，并通过 GD32 Boot ID
与 request_id关联。

2026-09-14 GD32完整 Agent固件已在 Linux成功交叉链接：Flash区域256,080 B，SRAM区域
17,384 B；新增命令包括 `query`、`control`、`auto`、`chat`、`nodes`、`topics`和
`stats`。该构件随后已完成 Windows烧录和单串口三逻辑节点业务实测。
实测覆盖 Discovery、温度查询、风扇手动控制、自动控制、LLM Mock请求/响应关联和动作
执行，当前 Windows模拟 + GD32 Agent业务闭环通过。测试启动时服务已处于运行状态并提示
`UMCA GD32 service is already running`，因此观测值是该服务运行期间的累计统计，不作为
本轮独立帧数；所有关键错误计数均为0。该阶段未覆盖的真实 ESP32三串口固件和MiMo
Provider已于2026-09-18在最终混合实物拓扑中通过验收。
按原理图完成的扩展配置审计已在编译期禁用不适用 LQFP100 的 SPI5、
ENET、EXMC、TLI 和 DCI 旧映射；200 MHz Profile 下禁用需要精确 48 MHz
时钟域的 USB FS、SDIO 和 TRNG。板载 LED 修正为 PC6，低有效用户按键修正为
PA0 并使用双边沿 IRQ。
Linux 负责编译，Windows 负责 GD-Link/CMSIS-DAP 烧录和 GDB 调试；`.vscode/` 未修改。
同时已修复本地 NuttX GD32F4 早期串口初始化对空槽位的空指针解引用；该公共仓改动
与参赛仓代码分开维护，本地提交为 `c42f0d1da26`（未推送）；GD32 全量编译和
NuttX `checkpatch.sh` 已通过。

尚未实现的 Core能力保持关闭：分段、RPC、ACK/ERROR、QoS-1/2/3、通用动态网关、
多路径、安全和动态内存。ESP32固定路由属于比赛应用，不代表 Core已经支持任意拓扑。
Goldfish Loopback资源数据不得外推为 GD32资源结论。

2026-09-14 已生成独立 `umca-node-sdk-v0.1.2-20260914` 节点对接包，包含可移植 Core、
UART/Loopback PHY、POSIX PAL、共享业务 Codec、协议/节点移植文档、固定向量及 Windows
参考工具，不包含板级固件、PDF、采集数据、凭据或 `.vscode/`。归档解包后已通过
MINIMAL 3/3、DISCOVERY 4/4、CONTEST 4/4 CTest及 Windows 14/14测试。

## 最终GD32 Agent集成与验收（2026-09-18）

`sensor_query`和`device_control`通过
`packages/ai_agent/src/tools/tool_registry.h`定义的真实external provider API注册和
执行；MCU仅链接静态单provider轻量后端，不引入完整网络/TLS ai_agent。`chat`在线路上
仍使用原`/llm/chat/request` V1 Payload，但其text统一封装为应用层
“LlmChatRequest V1 Compact Context C1”。CLI仍接收原始用户文本，C1拒绝非法UTF-8、
控制字符和超过动态剩余容量的输入，`tv=0`时不携带旧温度。

LLM `set_fan`建议经过GD32授权并通过Tool调用既有FanCommand/FanState；非零status、
迟到、关联错误、Actuator离线或反馈失败均不能形成未经确认的动作。新增
`agent-auto on|off|status`，默认关闭，与本地auto互斥，事件邮箱容量1、冷却60秒。
本地query/manual/auto不依赖网络。

最终自动化结果：MINIMAL 3/3、DISCOVERY 5/5、CONTEST 5/5、Windows 14/14均通过，
Python语法和`git diff --check`通过。GD32全量交叉构建为Flash 262,572 B、SRAM
17,668 B，text/data/bss为261,403/1,168/16,500 B；相对上一基线增加6,492/284 B。
最终二进制已在Windows端完成烧录，并通过混合实物拓扑功能验收。最终拓扑中GD32、
ESP32-S3和Actuator为实体节点，Sensor由Windows串口模拟；ESP32固定路由及MiMo云端
API均已验收。UMCA线级协议未因该验收发生修改。

本次验收将Sensor的软件实现正式确定为比赛交付方案，而非临时替代：Windows进程使用
Sensor DevID发布温度与阈值Topic，ESP32仍将其视为独立UMCA叶节点。早期单COM三节点
Mock结果继续作为无实体网关时的回归证据，不再代表最终硬件组成。

2026-09-18 已生成最终节点对接包 `umca-node-sdk-v0.1.2-20260918`，tar.gz 的
SHA-256 为 `874f927b8d21dcf14df9d1c4771ce454199942be9655e91686e54989dc3b1335`，
zip 的 SHA-256 为 `256bf4841840b43967bcf6ebda515b1357c215f721b1eea2967a8a23c83b59b0`。
两个归档均已通过压缩完整性和内部 Manifest 校验；`dist/SHA256SUMS` 是校验入口。
