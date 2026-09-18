# UMCA：面向 openvela 的多节点智能控制系统

> 2026 首届 openvela AI 硬件开发者大赛，队伍 `417_duwen`

UMCA（Unified Modular Communication Architecture，统一模块化通信架构）是一套面向
MCU、传感器、执行器和网络网关的轻量通信中间件。本项目在 openvela 上实现了 UMCA
MVP，并以 GD32F470VKT6 作为本地汇聚控制中心，通过 ESP32-S3 网关接入 Sensor、
Actuator 以及云端 MiMo。

当前已经完成可移植协议 Core、Discovery、UART PHY、GD32 Agent、`ai_agent` Tool
接入、ESP32-S3 固定路由和 MiMo 云端 API 闭环。最终验收拓扑中 GD32、ESP32-S3 和
Actuator 均为实体节点，Sensor 则由 Windows 通过串口模拟为独立 UMCA 逻辑节点。

## 一、作品简介

### 1.1 选题方向

本作品属于 **AI 硬件产品创新**。它解决异构嵌入式节点之间协议割裂、设备发现困难、
控制链路与云端 AI 耦合的问题，并提供以下能力：

- 使用统一 UMCA 帧和 Topic 连接 GD32、Sensor、Actuator 与 ESP32；
- 使用静态容量和可裁剪 Profile 适配资源受限 MCU；
- 由 GD32 保存可信设备状态并掌握最终控制授权；
- 通过 ESP32-S3 调用云端 MiMo，但网络故障不影响本地确定性控制；
- 提供可独立交付的节点 SDK、协议文档、固定测试向量和 Windows 参考工具。

### 1.2 关键设计

```text
Windows Sensor模拟 -- USB-UART --+
                                  |
GD32F470VKT6 ------- UART --------+-- ESP32-S3-N16R8 -- Wi-Fi/HTTPS -- MiMo API
                                  |
实体Actuator -------- UART -------+
```

- **GD32**：openvela 本地控制中心，维护节点状态和温度缓存，执行安全授权。
- **ESP32-S3**：固定三串口路由网关，同时是 UMCA LLM 节点和 MiMo HTTPS 客户端。
- **Sensor**：由 Windows 串口模拟，使用独立 DevID 发布温度和阈值事件。
- **Actuator**：接收风扇命令并返回执行状态，不具备网络能力。
- **MiMo**：仅返回文本和有限动作建议，不直接控制执行器。

完整 AI 闭环为：

```text
TemperatureSample -> GD32可信缓存 -> sensor_query Tool
                  -> LlmChatRequest V1/C1 -> ESP32/MiMo
                  -> LlmChatResponse -> GD32校验
                  -> device_control Tool -> FanCommand -> FanState
```

## 二、功能覆盖与完成状态

| 模块 | 状态 | 覆盖范围 |
|------|------|----------|
| UMCA Core | 完成 | 固定帧、CRC32C、Topic ID、QoS-0 Pub/Sub、Sequence、静态资源 |
| Discovery | 完成 | ANNOUNCE、HEARTBEAT、TEARDOWN、Boot ID 会话、节点上下线 |
| PHY/PAL | 完成 | Loopback PHY、UART 字节流 PHY、POSIX PAL、openvela PAL |
| Goldfish 三节点 | 完成 | Sensor、Agent、Actuator Loopback 业务闭环 |
| GD32 UART3 | 实板通过 | ANNOUNCE、HEARTBEAT、DATA、CRC、半帧、粘包和断线恢复 |
| GD32 Agent | 实板通过 | 查询、手动/自动控制、Chat、Tool Registry、C1、agent-auto |
| Windows 三节点 Mock | 回归通过 | 历史阶段单 COM 模拟 Sensor、Actuator、ESP32 和 LLM Mock |
| COM 与 TCP 网桥 | 实板通过 | 双向透明转发、分包/粘包、断线清空、固定缓冲溢出统计 |
| 节点开发 SDK | 完成 | 可移植源码、Codec、文档、向量和参考测试 |
| 实体 ESP32-S3 | 实板通过 | 固定串口路由、Wi-Fi/TLS 和 MiMo Provider |
| 实体 Actuator | 实板通过 | FanCommand、FanState 和执行确认 |
| Windows Sensor | 最终方案通过 | 串口模拟温度与阈值事件发布 |
| MiMo 云端 API | 验收通过 | C1 请求、文本回复和有限动作建议闭环 |

最终 GD32 固件包含两个真实 `ai_agent` external provider Tool：

- `sensor_query`：读取请求创建时的 GD32 可信温度快照；离线、过期或质量无效时不输出旧温度。
- `device_control`：只接受 `fan/on` 或 `fan/off`，验证 Actuator Source、Boot ID、
  `request_id`、结果与超时后才确认动作。

Chat 使用现有 `LlmChatRequest V1`，没有修改 UMCA 线级协议。GD32 在 V1 的 `text`
字段中生成 `LlmChatRequest V1 Compact Context C1`，把经过在线、新鲜度和质量检查的
设备快照连同用户问题交给 MiMo。

## 三、硬件与接口

目标开发板为 **GD32F470V-START V1.0**，MCU 为 **GD32F470VKT6**。

| 用途 | 外设 | TX | RX | 参数 | openvela 设备 |
|------|------|----|----|------|---------------|
| NSH 控制台 | USART0 AF7 | PB6 | PB7 | 115200 8N1 | `/dev/ttyS0` |
| UMCA 链路 | UART3 AF8 | PC10 | PC11 | 115200 8N1 | `/dev/ttyS1` |

UMCA 联调接线：

```text
GD32 PC10 / UART3_TX -> CH340或ESP32 RX
GD32 PC11 / UART3_RX <- CH340或ESP32 TX
GD32 GND              -- 对端 GND
```

电平必须为 3.3 V TTL，不使用 RTS/CTS，当前基线不使用 DMA。PC10/PC11 同时复用
SDIO_D2/SDIO_D3，因此本配置必须保持 SDIO 关闭。PA9 接板载 USB VBUS 检测网络，
不得作为本项目串口 TX。

详细原理图核对、排针位置、时钟和烧录说明见
[GD32F470VKT6_Hardware_Bringup.md](docs/GD32F470VKT6_Hardware_Bringup.md)。

## 四、仓库结构

```text
app/openvela_umca_gd32/   GD32 NSH应用、Agent和轻量Tool Registry后端
app/openvela_umca_agent/  Goldfish/openvela Agent应用与适配层
app/umca_demo/            Sensor、Actuator和LLM共享业务Payload Codec
umca/                     平台无关UMCA Core、PHY、PAL及测试
scripts/windows/          UART测试、透明网桥和单串口三节点模拟器
scripts/build_gd32_umca.sh
                          GD32完整交叉构建脚本
packaging/umca-node-sdk/  独立节点SDK打包定义
dist/                     已生成的SDK归档及SHA-256
docs/                     架构、协议、设计、Bring-up和节点对接文档
logs/                     比赛要求的AI Coding日志
```

模板中的 `hello_app`、`hello_quickapp` 和 `contest_board` 不属于 UMCA 作品运行路径。

## 五、获取完整 openvela 工作区

按照组委会 manifest 拉取完整工程：

```bash
repo init -u https://github.com/open-vela/contest2026_417_duwen \
  -b dev-ai-contest-2026 -m contest2026_417_duwen.xml
repo sync -c -j8
```

同步后，本仓位于工作区的 `contest2026_417_duwen/`，openvela 的 `nuttx/`、`apps/`、
`packages/`、`vendor/` 和 `prebuilts/` 位于其上一级。manifest 会将 UMCA 相关目录映射
到 openvela 构建树，不需要手工复制源码。

## 六、主机快速验证

主机测试只需要 CMake、C 编译器和 Python 3，不依赖 GD32 硬件。

```bash
cd contest2026_417_duwen

for profile in MINIMAL DISCOVERY CONTEST; do
  cmake -S umca -B "/tmp/umca-${profile}" \
    -DUMCA_PROFILE="${profile}" -DBUILD_TESTING=ON
  cmake --build "/tmp/umca-${profile}" -j4
  ctest --test-dir "/tmp/umca-${profile}" --output-on-failure
done

python3 -m unittest scripts/windows/test_windows_tools.py -v
```

当前基线结果：

| 测试集 | 结果 |
|--------|------|
| MINIMAL CTest | 3/3 |
| DISCOVERY CTest | 5/5 |
| CONTEST CTest | 5/5 |
| Windows 工具测试 | 14/14 |

## 七、GD32 固件构建与烧录

### 7.1 Linux 交叉构建

在比赛仓目录执行：

```bash
BUILD_JOBS=4 ./scripts/build_gd32_umca.sh
```

脚本基于 GD32F470V-START `nsh` 配置，启用 UMCA Discovery Profile、UART3 和 GD32
应用，并关闭冲突或不适用的外设。产物位于工作区 `nuttx/`：

```text
../nuttx/nuttx.elf
../nuttx/nuttx.bin
../nuttx/nuttx.hex
../nuttx/nuttx.map
```

2026-09-18 最终 Linux 构建资源：

| 指标 | 大小 |
|------|------|
| Flash | 262,572 B |
| SRAM region | 17,668 B |
| text / data / bss | 261,403 / 1,168 / 16,500 B |

### 7.2 Windows 烧录和调试

使用 GD-Link 或 CMSIS-DAP 在 Windows 端烧录 `nuttx.bin` 或 `nuttx.hex`，使用
`nuttx.elf` 加载符号。Linux 端只负责编译，不在本流程中烧录。

OpenOCD 兼容基线：

```powershell
openocd.exe -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg `
  -c "program nuttx.bin verify reset exit 0x08000000"
```

GD32F470 的实际识别和 Flash 算法以本机 OpenOCD 或厂商工具输出为准。

最终镜像已经完成 Windows 端烧录，并在“实体 GD32 + 实体 ESP32-S3 + 实体
Actuator + Windows 串口 Sensor”拓扑中通过功能验收。此前 UART 与业务闭环实测均未
出现 HardFault；长期压力与故障注入结果仍应作为独立可靠性记录保存。

## 八、硬件闭环演示

最终演示使用实体 GD32、实体 ESP32-S3 和实体 Actuator。Sensor 不再单独制作实体节点，
而是由 Windows 通过 USB-UART 连接 ESP32 的 Sensor 入口，发布同一 UMCA Sensor DevID
和业务 Topic。ESP32 负责串口域路由并通过 Wi-Fi/HTTPS 调用已验收的 MiMo 云端 API。

### 8.1 历史三节点 Mock 回归

以下命令保留用于不连接实体 ESP32/Actuator 时的 GD32 单串口回归，不代表最终拓扑：

```powershell
py -m pip install -r scripts\windows\requirements.txt
py scripts\windows\umca_three_node_sim.py `
  --port COM7 --baud 115200 `
  --evidence gd32-final-agent.jsonl
```

`COM7` 是当前测试机端口，复现时按设备管理器中的实际端口修改。

### 8.2 最终混合实物验收

通过独立的 USART0 串口进入 NSH：

```text
umca_gd32 pinout
umca_gd32 start 0x4700000000000001 1
umca_gd32 status
umca_gd32 nodes
umca_gd32 topics
umca_gd32 query
umca_gd32 chat 当前温度是多少？
umca_gd32 chat turn fan on
umca_gd32 control off
umca_gd32 stats
```

`query`、`control` 和本地 `auto` 均不依赖网络。`agent-auto` 默认关闭，并与本地
`auto` 互斥；测试主动评估前先关闭本地控制：

```text
umca_gd32 auto off
umca_gd32 agent-auto on
umca_gd32 agent-auto status
umca_gd32 stats
umca_gd32 agent-auto off
umca_gd32 auto on
umca_gd32 stop
```

最终验收时应观察：Windows Sensor、实体 Actuator 和实体 ESP32 三个 UMCA 节点上线、
温度缓存有效、C1 Chat 包含可信温度、真实 MiMo 返回文本或有限动作建议、
`sensor_query` 被调用、`device_control` 返回 `confirmed`、FanState 与请求匹配，且 CRC、
长度、语义、重复、陈旧和容量错误保持符合预期。

更完整的 UART 原始帧、COM 与 TCP 网桥及故障注入步骤见
[Windows_UART_TCP_Validation.md](docs/Windows_UART_TCP_Validation.md)。

## 九、节点 SDK 与旧版兼容性

最终节点开发包：

| 归档 | SHA-256 |
|------|---------|
| [tar.gz](dist/umca-node-sdk-v0.1.2-20260918.tar.gz) | `874f927b8d21dcf14df9d1c4771ce454199942be9655e91686e54989dc3b1335` |
| [zip](dist/umca-node-sdk-v0.1.2-20260918.zip) | `256bf4841840b43967bcf6ebda515b1357c215f721b1eea2967a8a23c83b59b0` |

外层校验文件为 [`dist/SHA256SUMS`](dist/SHA256SUMS)。

校验方式：

```bash
cd dist
sha256sum -c SHA256SUMS
```

SDK 包含平台无关 Core、UART/Loopback PHY、POSIX PAL、业务 Codec、协议和移植文档、
固定向量以及 Windows 参考工具。其他节点的接入流程见
[UMCA_Portable_Node_Development_Guide.md](docs/UMCA_Portable_Node_Development_Guide.md)。

ESP32 使用旧版 UMCA SDK 时，线级协议保持兼容：帧头、CRC、Topic、
`LlmChatRequest V1`、`LlmChatResponse` 和 241/243 字节上限均未改变。需要注意：

- 新 GD32 在线路上发送的仍是 V1 Payload，但 `text` 内容统一封装为 C1；
- 旧 ESP32 应用必须完整转发 `text`，不得截断、删除 C1 或只保留 `Q:`；
- MiMo system prompt 必须解释 C1、`tv=0`、`f=2` 和 `none|set_fan` 动作限制；
- 因此通常无需升级旧 UMCA Core，但需要确认或更新 ESP32 Provider 应用层。

ESP32、Sensor 和 Actuator 的 DevID、Topic、Payload 与建议引脚分配见
[UMCA_Node_Integration_Guide.md](docs/UMCA_Node_Integration_Guide.md)。

## 十、文档导航

| 文档 | 内容 |
|------|------|
| [架构设计](docs/UMCA_Architecture_Design_v0.3.3.md) | 分层模型、节点角色、资源和边界 |
| [协议规范](docs/UMCA_Protocol_Specification_v0.1.2.md) | 帧格式、CRC、Discovery、Topic 和 Payload |
| [软件设计](docs/UMCA_Software_Design_v0.1.2.md) | 模块、接口、状态机、Agent 和 C1 设计 |
| [开发计划](docs/UMCA_Development_Plan_v0.1.2.md) | 阶段目标、门禁、风险和验证记录 |
| [实现状态](docs/UMCA_Implementation_Status.md) | 已完成能力、实测证据和未完成项 |
| [GD32 Bring-up](docs/GD32F470VKT6_Hardware_Bringup.md) | 原理图复核、构建、接线、烧录和调试 |
| [节点对接指南](docs/UMCA_Node_Integration_Guide.md) | ESP32/Sensor/Actuator 接口契约 |
| [可移植开发指南](docs/UMCA_Portable_Node_Development_Guide.md) | 其他 MCU/RTOS 节点移植流程 |

## 十一、AI Coding 使用说明

本项目在需求拆解、协议审计、固定测试向量、Core 与 PHY 实现、GD32 原理图核对、
UART 故障定位、Windows 测试工具、Agent 安全边界、回归测试和文档同步过程中使用了
AI 辅助开发。关键设计结论均通过源码测试、交叉构建、逻辑分析仪/UART 数据或实板
调试验证，没有用模型输出替代硬件验收。

完整 AI Coding 会话记录位于 [`logs/`](logs/)，日志格式和导出规则见
[`logs/README.md`](logs/README.md)。比赛规则、提交流程和 AI 日志要求以 openvela 官方
文档为准：

- [大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
- [参赛代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)
- [AI Coding 日志归集与提交手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)

## 十二、当前边界

- Sensor 最终由 Windows 串口模拟，不交付独立实体 Sensor 固件；
- 实体 ESP32-S3、实体 Actuator、最终 GD32 固件和 MiMo 云端 API 已完成闭环验收；
- MVP 不实现分段重组、QoS-1/2/3、通用 ACK/ERROR、RPC、动态路由、多路径和安全层；
- ESP32 固定路由是比赛应用拓扑，不代表当前 Core 支持任意多跳网络；
- UMCA Core 运行期不使用动态内存，容量在编译期固定，超限时明确返回错误；
- 云端不可用时，本地 `query`、手动控制和温度迟滞控制仍可独立运行。

后续工作以验收证据归档和混合实物拓扑的长期稳定性测试为主，不再扩展新的 UMCA
线级版本或通用 Tool Calling。
