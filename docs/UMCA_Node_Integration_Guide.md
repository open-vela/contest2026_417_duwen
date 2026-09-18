# UMCA 节点与 ESP32-S3 网关对接说明

## 1. 文档范围

本文档冻结比赛演示中 GD32 Agent、Sensor、Actuator 和 ESP32-S3 LLM 网关之间的
身份、物理接口、Topic、Payload、路由和超时约束。其他节点开发者只需实现本文档
规定的线级行为，不得发送 C 结构体内存镜像。

当前最终拓扑为固定星形：ESP32-S3-N16R8是三条点对点串口的中心网关，GD32和
Actuator是实体叶节点，Sensor是Windows通过USB-UART承载的逻辑叶节点。ESP32同时
通过Wi-Fi/HTTPS调用MiMo API；该混合实物拓扑及云端API已完成验收。

```text
                         MiMo API
                            ^
                            | Wi-Fi + HTTPS
                            |
Windows Sensor -- USB-UART/RMT -- ESP32-S3 gateway -- hardware UART -- GD32 Agent
                                  |
                                  +-- hardware UART -- Actuator

PC terminal -- USB-TTL -- GD32 USART0
```

这不是把多个 TTL TX 引脚并联到同一根线上。三条链路均为独立 TX/RX 点对点连接。

## 2. 固定身份

| 节点 | DevID | 说明 |
|------|-------|------|
| GD32 Agent | `0x4700000000000001` | 控制、安全校验和终端交互 |
| Sensor | `0x0000000000001001` | Windows串口模拟的温度和阈值事件发布者 |
| Actuator | `0x0000000000003001` | 风扇命令执行者 |
| ESP32-S3 | `0x3300000000000001` | 固定路由网关和MiMo客户端 |
| Broadcast | `0xFFFFFFFFFFFFFFFF` | Discovery 和允许广播的 DATA |

DevID 在设备生命周期内稳定。Boot ID 是独立非零 `u32`，每次启动或新 UMCA 会话必须
更换，禁止把 Boot ID 拼接进 DevID。每个节点分别维护自己的系统 Sequence，以及每个
Publisher Topic 的 DATA Sequence。

## 3. 物理接口与引脚

所有链路默认 8 data bits、无校验、1 stop bit、无 RTS/CTS、3.3 V CMOS TTL并共地。
不得直接连接 RS-232 电平或 5 V TTL。

### 3.1 GD32F470V-START

| 用途 | 实例 | TX | RX | 波特率 | 设备 |
|------|------|----|----|--------|------|
| NSH/用户对话 | USART0 AF7 | PB6 | PB7 | 115200 | `/dev/ttyS0` |
| ESP32 UMCA | UART3 AF8 | PC10 | PC11 | 115200 | `/dev/ttyS1` |

PC10/PC11 与 SDIO_D2/SDIO_D3 复用，当前 Profile 必须保持 SDIO关闭。PA9接板载
USB VBUS检测，PD2接 USB电源控制网络，均不得用于本设计的串口。

### 3.2 ESP32-S3-N16R8

下表是本项目分配。ESP32-S3 GPIO Matrix 应把硬件 UART信号映射到指定 GPIO。

| 用途 | ESP32资源 | ESP32 TX | ESP32 RX | 波特率 |
|------|-----------|----------|----------|--------|
| 烧录/启动日志 | UART0 | GPIO43 | GPIO44 | 115200 |
| GD32上行 | UART1 | GPIO17 | GPIO18 | 115200 |
| Actuator | UART2 | GPIO15 | GPIO16 | 115200 |
| Windows Sensor | RMT辅助软件UART | GPIO7 | GPIO8 | 38400 |

交叉接线：ESP32 TX 接对端 RX，ESP32 RX 接对端 TX。USB D-/D+ 的 GPIO19/GPIO20、
启动绑定位 GPIO0/GPIO3/GPIO45/GPIO46、传统 JTAG GPIO39~GPIO42，以及 N16R8
Flash/PSRAM占用的内部引脚均不分配给节点链路。

Windows Sensor通过3.3 V USB-UART连接GPIO7/GPIO8对应入口。ESP32侧采用RMT或等价
硬件辅助采样，不允许依赖长时间关中断的忙等bit-bang。波特率固定为38400；若
Wi-Fi/TLS并发压力测试出现丢字节，应改用外接UART扩展器，不得通过关闭CRC或放宽帧
校验掩盖问题。

## 4. ESP32固定路由行为

ESP32维护固定容量出口表：

| Destination | 处理/出口 |
|-------------|-----------|
| GD32 DevID | UART1 |
| Sensor DevID | Windows Sensor软件UART入口 |
| Actuator DevID | UART2 |
| ESP32 DevID | ESP32本地 UMCA应用 |
| Broadcast | 本地处理，并转发到除入口外的其他活动端口 |

转发行为必须：

1. 完整验证 Magic、Version、长度、Flags、QoS和 CRC。
2. 只转发 TTL大于1的帧，发出前 TTL减1并重新计算 CRC32C。
3. 保持 Source、Destination、Topic、Sequence和 Payload不变。
4. 单播只发往唯一配置出口，不向无关叶节点泛洪。
5. 广播不发回入口端口；当前固定星形不允许级联网关形成环路。
6. 每个入口使用固定容量接收缓冲和完整帧队列，满时丢弃新帧并计数。
7. LLM/HTTPS任务不得阻塞串口路由任务。

因此最终网关不是纯字节透明桥。历史Windows COM↔TCP工具仍用于验证字节流边界；实体
ESP32跨端口路由已经通过最终混合实物验收。

## 5. Topic注册表

| Topic | ID | Publisher | Subscriber |
|-------|----|-----------|------------|
| `/sensors/temperature` | `0x0FC95CB0` | Sensor | GD32 |
| `/events/temperature/threshold` | `0x5389C486` | Sensor | GD32 |
| `/actuators/fan/command` | `0xC72FE51A` | GD32 | Actuator |
| `/actuators/fan/state` | `0x6D3F9EBE` | Actuator | GD32 |
| `/llm/chat/request` | `0x7985C52E` | GD32 | ESP32 |
| `/llm/chat/response` | `0x9D3E1F72` | ESP32 | GD32 |

Topic ID使用规范 Topic字符串的 FNV-1a 32位结果。节点启动时先注册全部 Topic，再发送
ANNOUNCE；之后才允许发送 DATA。

## 6. 业务Payload

所有多字节整数均为网络字节序。温度、阈值和风扇 Payload沿用协议规范中的固定
9/13/6/10字节定义。

### 6.1 LlmChatRequest

Topic：`/llm/chat/request`，消息类型：`DATA`，Destination：ESP32 DevID。

| 偏移 | 字段 | 类型 | 约束 |
|------|------|------|------|
| 0 | `requester_boot_id` | `u32` | 当前 GD32 Boot ID，非0 |
| 4 | `request_id` | `u32` | GD32生成，非0 |
| 8 | `timeout_ms` | `u32` | 非0，当前默认15000 |
| 12 | `origin` | `u8` | 0终端，1自动事件 |
| 13 | `text_length` | `u16` | 1~241 |
| 15 | `text` | bytes | UTF-8，长度等于`text_length` |

总 Payload不得超过256字节。该请求是 QoS-0 DATA，不提供 UMCA传输层自动重试；这里的
“关联”仅指通过 Boot ID和 request_id拒绝迟到、重复或跨会话响应。

### 6.2 LlmChatResponse

Topic：`/llm/chat/response`，消息类型：`DATA`，Destination：GD32 DevID。

| 偏移 | 字段 | 类型 | 约束 |
|------|------|------|------|
| 0 | `requester_boot_id` | `u32` | 原样返回请求值 |
| 4 | `request_id` | `u32` | 原样返回请求值 |
| 8 | `status` | `u8` | 0成功、1拒绝、2不可用、3超时、4内部错误 |
| 9 | `action_type` | `u8` | 0无动作、1设置风扇 |
| 10 | `action_value` | `u8` | `action_type=1`时0关/1开，否则必须为0 |
| 11 | `text_length` | `u16` | 0~243 |
| 13 | `text` | bytes | UTF-8，长度等于`text_length` |

ESP32必须把云端返回转换为上述有限动作，不得把脚本、函数名、地址或任意二进制执行
内容发送给 GD32。GD32再次检查来源 DevID、Boot ID、request_id、status、动作白名单、
参数范围和 Actuator在线状态；通过后才生成新的 FanCommand request_id。

## 7. 时间和失败语义

- Sensor默认每1000 ms发布一次温度，样本在 GD32缓存中最多有效5000 ms。
- 每个节点每5000 ms发送 HEARTBEAT，每30000 ms重新发送 ANNOUNCE。
- 15秒未见有效心跳后节点转为 OFFLINE。
- FanCommand默认等待 FanState 1000 ms；只接受正确 Source和 request_id且状态吻合的确认。
- LLM请求默认等待15000 ms；同一时刻 GD32只保留一个 Chat请求。
- Wi-Fi、DNS、TLS、HTTP或MiMo失败不得阻塞串口转发；ESP32返回明确失败状态或让 GD32
  超时，本地温度规则继续工作。
- ESP32重启后使用新 Boot ID并重新 ANNOUNCE；不得恢复或重放旧 LLM动作。

## 8. Windows模拟与最终拓扑

早期回归工具`scripts/windows/umca_three_node_sim.py`可通过一个COM端口同时模拟
ESP32、Sensor和Actuator三个逻辑节点：

```powershell
py scripts\windows\umca_three_node_sim.py `
  --port COM7 --baud 115200 `
  --evidence umca-three-node-evidence.jsonl
```

脚本使用三个独立 DevID、Boot ID、系统 Sequence和 DATA Sequence。Sensor和Actuator
发往 GD32的帧使用 TTL=7，表示已经经过 ESP32一次转发；ESP32自身帧使用 TTL=8。
当前 LLM Provider是确定性 Mock：`turn fan on`、`turn fan off`、`打开风扇`和`关闭风扇`
产生有限动作，其余文本只返回响应。

该工具继续用于没有ESP32/Actuator硬件时的GD32自动回归。最终验收不再模拟全部三个
远端角色，而是只由Windows承载Sensor；ESP32-S3和Actuator均为实体节点。实体ESP32
串口路由、Wi-Fi/TLS及MiMo云端API已经验收通过。API Key仍不得写入仓库、源码、
命令行或测试证据。

最终Sensor虽然运行在Windows上，但必须表现为独立UMCA节点：使用固定Sensor DevID、
独立非零Boot ID、系统Sequence和DATA Sequence，周期发布TemperatureSample，并在
阈值跨越时发布TemperatureThresholdEvent。不得借用ESP32 DevID或由ESP32伪造Source。

## 9. LlmChatRequest V1 Compact Context C1

GD32最终Agent在既有`/llm/chat/request` V1的`text`字段中使用C1：

```text
C1;tv=1;t=<int32>;age=<uint32>;q=<uint8>;so=<0|1>;ao=<0|1>;f=<0|1|2>
Q:<用户UTF-8文本>

C1;tv=0;so=<0|1>;ao=<0|1>;f=<0|1|2>
Q:<用户UTF-8文本>
```

这是V1文本字段内部的应用层约定，不是UMCA线级V2。ESP32仍按原V1解码请求头，且无需
自行解析C1或判断温度有效性，只需把完整text交给MiMo。字段顺序不可变化；`tv=0`时
不存在`t/age/q`，`f=0/1/2`分别表示off/on/unknown。总text长度仍不得超过241字节。

实体ESP32 Provider的固定system prompt必须说明：C1是GD32提供的可信设备快照；
`tv=0`时不得编造温度；`f=2`表示未知；回复text保持简短；动作仅允许`none`或
`set_fan`。Windows Mock已按这些规则严格解析并测试，实体ESP32 Provider及MiMo云端
API也已完成最终验收。

动作只是建议。ESP32不得发布FanCommand；GD32校验响应Source、ESP32 Boot ID、
requester_boot_id、request_id、deadline和动作白名单后，才通过`device_control` Tool
发布FanCommand并等待Actuator FanState。
