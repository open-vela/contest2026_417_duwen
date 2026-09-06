# UMCA 初版协议规范

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档名称 | UMCA 初版协议规范（UMCA Protocol Specification） |
| 版本 | **v0.1.2-draft** |
| 日期 | 2026-08-22 |
| 架构基线 | `UMCA_Architecture_Design_v0.3.3.md` |
| 目标 | openvela AI 硬件比赛 MVP |
| 首期平台 | Goldfish ARM64 模拟器、POSIX/Linux 单元测试 |
| 后续目标平台 | GD32F470V-START（MCU：GD32F470VKT6） |
| 状态 | 正式开发前协议冻结候选基线 |

### v0.1.2修订摘要

- PHY改为`ops + driver`实例描述符，支持多个Loopback或硬件PHY实例。
- PAL回调增加`user`上下文，mutex改为宿主创建并注入。
- ANNOUNCE、HEARTBEAT和TEARDOWN统一携带非零32位`boot_id`。
- 同一DevID更换`boot_id`时建立新会话并清除对应Sequence历史。
- MVP严格要求`Flags=0`、`QoS=0`，不执行任何静默降级。
- ERROR、ACK、REQUEST和RESPONSE保留但不实现。
- Topic字符集冻结为可移植ASCII子集。
- 统一Topic注册API、PHY返回值、错误码和节点表复用语义。
- 增加可独立复核的CRC、Topic和完整帧固定测试向量。
- 固定帧头、CRC规则、Topic ID算法和`Version=0x01`保持不变。

---

## 1. 范围与状态标记

本文档将UMCA v0.3.3架构收敛为比赛阶段可实现、可测试、可演示的初版协议。

状态标记定义：

| 标记 | 含义 |
|------|------|
| **[MVP]** | 初版协议必须实现并测试 |
| **[Demo]** | 比赛演示场景必须实现 |
| **[预留]** | 线级字段保留，但本版本不执行对应策略 |
| **[未实现]** | 本版本仅标注，不定义实现细节 |

本文档中的“必须”“不得”“应”“可以”分别表示强制要求、禁止行为、推荐行为和可选行为。

---

## 2. 初版协议目标

### 2.1 必须实现

- **[MVP]** 固定线级帧编解码。
- **[MVP]** CRC32C 完整性校验。
- **[MVP]** 静态内存和固定容量表。
- **[MVP]** Broker-less Pub/Sub。
- **[MVP]** Loopback PHY。
- **[MVP]** openvela PAL。
- **[MVP]** POSIX/Linux PAL 和主机单元测试。
- **[MVP]** ANNOUNCE、HEARTBEAT、TEARDOWN 服务发现。
- **[MVP]** 节点上线、离线和 Topic 碰撞检测。
- **[Demo]** 虚拟传感器、Agent、虚拟执行器三节点闭环。
- **[Demo]** `sensor_query` 和 `device_control` 两个 ai_agent Tool/Skill。

### 2.2 实现原则

- UMCA Core 不得直接依赖 openvela、POSIX、UART、ai_agent 或具体硬件驱动。
- Core 仅依赖标准 C 基础类型、PAL 和 PHY Adapter 接口。
- 初版禁止动态内存分配。
- 发送和接收路径不得静默截断数据。
- 非法帧不得进入 Topic 回调。
- ai_agent 通过独立适配层接入 UMCA，不得侵入 UMCA Core。

---

## 3. 比赛 MVP 拓扑

初版在同一 openvela 模拟器进程内创建三个逻辑节点：

```text
┌────────────────────┐
│ Virtual Sensor Node│
│ 发布温度与阈值事件  │
└─────────┬──────────┘
          │
          ▼
┌────────────────────┐
│   Loopback PHY      │
│ UMCA 帧传输与发现    │
└──────┬────────┬────┘
       │        │
       ▼        ▼
┌────────────┐  ┌──────────────────┐
│ Agent Node │  │ Virtual Actuator │
│ ai_agent   │  │ 虚拟风扇          │
└────────────┘  └──────────────────┘
```

| 节点 | 建议 DevID | 职责 |
|------|------------|------|
| Virtual Sensor | `0x0000000000001001` | 发布温度和阈值事件 |
| Agent Node | `0x0000000000002001` | 查询传感器、生成决策、发布控制命令 |
| Virtual Actuator | `0x0000000000003001` | 接收风扇命令并发布执行状态 |

固定 DevID 只用于仿真。真实硬件必须使用生产写入 ID 或芯片唯一 ID 的稳定哈希。

---

## 4. 基础常量

| 常量 | 值 | 状态 |
|------|----|------|
| `UMCA_MAGIC` | `0x554D` | [MVP] |
| `UMCA_PROTOCOL_VERSION` | `0x01` | [MVP] |
| `UMCA_HEADER_SIZE` | `36` | [MVP] |
| `UMCA_CRC_SIZE` | `4` | [MVP] |
| `UMCA_FRAME_OVERHEAD` | `40` | [MVP] |
| `UMCA_MVP_MAX_PAYLOAD` | `256` | [MVP] |
| `UMCA_MVP_MAX_FRAME` | `296` | [MVP] |
| `UMCA_BROADCAST_DEVID` | `0xFFFFFFFFFFFFFFFF` | [MVP] |
| `UMCA_INVALID_DEVID` | `0x0000000000000000` | [MVP] |
| `UMCA_SYSTEM_TOPIC_ID` | `0x00000000` | [MVP] |
| `UMCA_DEFAULT_TTL` | `8` | [MVP] |
| `UMCA_MAX_NODES` | `8` | [MVP] |
| `UMCA_MAX_TOPICS` | `16` | [MVP] |
| `UMCA_MAX_TOPICS_PER_NODE` | `8` | [MVP] |
| `UMCA_RX_QUEUE_DEPTH` | `4` | [MVP] |
| `UMCA_TX_QUEUE_DEPTH` | `4` | [MVP] |

所有容量均允许在编译期调小或调大，但不同节点不得突破帧格式规定的长度上限。

---

## 5. 字节序与基础类型

- 所有多字节整数必须使用网络字节序，即大端序。
- 线级协议不得直接发送 C 结构体内存镜像。
- 编解码必须逐字段执行，禁止依赖编译器填充和对齐。
- 有符号整数采用二进制补码。
- 布尔值在线上使用 `u8`，0 表示 false，1 表示 true。
- 字符串使用 UTF-8，不携带结尾 `\0`。

---

## 6. 统一帧格式

### 6.1 帧布局

| 偏移 | 字段 | 类型 | 长度 | 约束 |
|------|------|------|------|------|
| 0 | Magic | `u16` | 2 | 固定 `0x554D` |
| 2 | Version | `u8` | 1 | 固定 `0x01` |
| 3 | Header Length | `u8` | 1 | 固定 `36`，包含 Magic 至 Payload 前 |
| 4 | Flags | `u16` | 2 | 未定义位必须为 0 |
| 6 | Message Type | `u8` | 1 | 见 7.1 |
| 7 | QoS | `u8` | 1 | 范围 0~3；初版仅执行 0 |
| 8 | TTL | `u8` | 1 | 默认 8 |
| 9 | Reserved | `u8` | 1 | 必须为 0 |
| 10 | Destination DevID | `u64` | 8 | 单播或广播 |
| 18 | Source DevID | `u64` | 8 | 不得为 0 |
| 26 | Topic ID | `u32` | 4 | 系统消息使用 0 |
| 30 | Sequence | `u32` | 4 | 按源节点和 Topic 独立递增 |
| 34 | Payload Length | `u16` | 2 | 0~256 |
| 36 | Payload | bytes | N | N 等于 Payload Length |
| 36+N | CRC32C | `u32` | 4 | 见 6.3 |

实际帧长必须严格等于：

```text
36 + Payload Length + 4
```

### 6.2 Flags

MVP要求Flags整个字段固定为`0x0000`。

| 位 | 名称 | 初版行为 |
|----|------|----------|
| 0 | `ACK_REQUIRED` | [未实现] 必须为0 |
| 1 | `ACK` | [未实现] 必须为0 |
| 2 | `ERROR` | [未实现] 必须为0 |
| 3~15 | Reserved | 必须为0 |

收到任意非零Flags的帧时必须拒绝，不得忽略或降级处理。

### 6.3 CRC32C

| 参数 | 值 |
|------|----|
| 算法 | CRC32C / Castagnoli |
| 反射多项式 | `0x82F63B78` |
| 初值 | `0xFFFFFFFF` |
| 结果异或 | `0xFFFFFFFF` |
| 覆盖范围 | Version 字段至 Payload 最后一个字节 |
| 不覆盖 | Magic 和 CRC 字段自身 |

CRC 不匹配时必须丢弃整帧，并增加本地 CRC 错误计数。

### 6.4 Sequence

Sequence 按以下键独立维护：

```text
<Source DevID, Topic ID>
```

- 新启动会话中每个键的首次发送Sequence为1。
- 发送成功后递增；发送失败不消耗Sequence。
- 达到`0xFFFFFFFF`后按32位无符号算术回绕到`0x00000000`。

接收规则：

1. 第一次收到某键的帧时直接接受。
2. Sequence 与上一帧相同，判定为重复帧并丢弃。
3. 按 32 位序列算术判断为更新的值时接受。
4. 出现跳号时接受新帧，同时累计估算丢包数。
5. 判断为旧值时作为过期帧丢弃。

节点会话变化规则：

- ANNOUNCE、HEARTBEAT和TEARDOWN携带32位`boot_id`。
- 同一Source DevID的合法ANNOUNCE携带不同`boot_id`时，表示节点进入了新启动会话。
- 接收方必须清除该Source DevID对应的全部Sequence历史，再接受新会话的ANNOUNCE。
- HEARTBEAT和TEARDOWN不得创建或切换会话，只在`boot_id`与当前会话一致时生效。
- 只有在完整服务发现Payload通过语义校验后，才能提交新`boot_id`和清除旧Sequence历史。
- DATA帧本身不携带`boot_id`；节点启动后必须先发送ANNOUNCE，再发布普通业务帧。
- v0.1.2不支持乱序的旧会话服务发现帧；MVP物理链路和Loopback必须保持发送顺序。

**[未实现]** 乱序重排。

---

## 7. 消息类型与通用语义

### 7.1 Message Type

| 值 | 名称 | Topic ID | Destination | 状态 |
|----|------|----------|-------------|------|
| `0x00` | DATA | 业务 Topic ID | 单播或广播 | [MVP] |
| `0x01` | REQUEST | 业务 Topic ID | 单播 | [未实现] |
| `0x02` | RESPONSE | 业务 Topic ID | 单播 | [未实现] |
| `0x03` | ANNOUNCE | 0 | 广播 | [MVP] |
| `0x04` | HEARTBEAT | 0 | 广播 | [MVP] |
| `0x05` | TEARDOWN | 0 | 广播 | [MVP] |
| `0x06` | ERROR | 原请求 Topic ID 或 0 | 单播 | [未实现] |
| `0x07~0xFF` | Reserved | - | - | [未实现] |

MVP语义校验矩阵：

- Destination DevID不得为`UMCA_INVALID_DEVID`。
- DATA的Topic ID必须非0，Destination可以是本节点单播或全局广播。
- ANNOUNCE、HEARTBEAT和TEARDOWN的Topic ID必须为0，Destination必须为全局广播。
- REQUEST、RESPONSE、ERROR及保留类型一律返回`UMCA_ERR_UNSUPPORTED_TYPE`并丢弃。
- Source DevID必须非0。

### 7.2 TTL

- 最终目标节点接收 TTL 大于 0 的帧时不得自行递减。
- 转发节点只允许转发 TTL 大于 1 的帧。
- 转发节点必须在发出前将 TTL 减 1。
- 初版 Loopback PHY 不执行多跳转发，但必须保留和校验 TTL 字段。

### 7.3 QoS

- 初版节点能力只宣告 QoS-0。
- MVP发送帧的QoS字段必须为0。
- 收到QoS-1~3帧时必须返回`UMCA_ERR_UNSUPPORTED_QOS`并丢弃，不得按QoS-0静默交付。
- 初版不得宣称确定性延迟、自动重传或可靠交付。

**[未实现]** QoS-1、QoS-2、QoS-3 策略。

### 7.4 ERROR消息

**[未实现]** MVP不发送、不解析和不回应ERROR消息，也不使用Flags中的ERROR位。

- 所有协议错误通过本地返回值、统计和可选日志报告。
- 收到Message Type为ERROR的帧时返回`UMCA_ERR_UNSUPPORTED_TYPE`并丢弃。
- FanCommand的应用执行结果通过FanState中的`result`字段返回。
- 后续版本如启用ERROR，必须重新冻结其Payload和与Flags的关系。

---

## 8. DevID

### 8.1 生成优先级

1. 生产环境写入的稳定 64 位 ID。
2. 芯片唯一 ID 经稳定哈希得到的 64 位值。
3. 仿真配置中的固定 ID。

### 8.2 冲突处理

- 节点收到与自身 DevID 相同、但来源不是自身回环发送的 ANNOUNCE 时，必须进入 ID 冲突状态。
- ID冲突状态下不得发送任何UMCA帧，包括DATA、HEARTBEAT、ANNOUNCE和TEARDOWN；节点只保留接收和本地诊断能力，直到重新配置并初始化。
- 仿真环境必须通过日志输出冲突双方的 DevID。

### 8.3 Boot ID

`boot_id`用于区分同一DevID的不同启动会话，解决节点重启后Sequence从初始值重新开始的问题。

- 类型为`u32`，使用网络字节序编码。
- `0x00000000`为无效值，不得在线上发送。
- 节点每次初始化新UMCA会话时生成一个新的非零值。
- 同一次运行期间必须保持不变。
- 优先使用平台随机数、持久化启动计数器或芯片唯一ID与启动信息的稳定混合结果。
- 仿真和单元测试可以注入固定值以保证结果可重复。
- `boot_id`不是认证凭据，不得作为安全身份使用。

---

## 9. Topic 与 Topic ID

### 9.1 Topic 规范化

MVP Topic使用UTF-8兼容的ASCII子集。完整名称必须满足：

- 以 `/` 开头。
- 大小写敏感。
- 根 Topic 以外不得以 `/` 结尾。
- 不允许连续的 `//`。
- 不允许路径段 `.` 和 `..`。
- 每个路径段只允许ASCII字母、数字、下划线`_`、连字符`-`和点号`.`。
- 不允许空路径段或任何非ASCII字节。
- 单个 Topic 名称在服务发现中最长 63 字节。

MVP不执行Unicode规范化。后续扩展到完整UTF-8前必须定义规范化形式和跨平台一致性测试。

### 9.2 Topic ID 算法

对规范化后的完整 Topic 字符串计算 FNV-1a 32 位哈希：

| 参数 | 值 |
|------|----|
| Offset basis | `0x811C9DC5` |
| Prime | `0x01000193` |

计算结果 `0x00000000` 为系统保留值，业务 Topic 得到该结果时必须更换 Topic 名称。

### 9.3 碰撞处理

- ANNOUNCE 必须同时携带 Topic ID 和 Topic 字符串。
- 相同 Topic ID 对应不同字符串时，必须标记 `UMCA_ERR_TOPIC_COLLISION`。
- 本地注册发生碰撞时拒绝新Topic；远端宣告发生碰撞时，冲突Topic不得进入远端发现表或本地路由决策。

---

## 10. 服务发现

### 10.1 节点状态

```text
UNKNOWN → ONLINE → STALE → OFFLINE
                    └────→ ONLINE
```

- 收到合法 ANNOUNCE 后进入 ONLINE。
- 未知DevID的HEARTBEAT不得创建节点表项，等待其ANNOUNCE。
- 无更新持续时间达到10000ms时标记STALE。
- 无更新持续时间达到15000ms时标记OFFLINE。
- 收到新的 ANNOUNCE 或 HEARTBEAT 后恢复 ONLINE。
- 收到同一DevID携带新非零`boot_id`的ANNOUNCE时建立新会话、清除旧Sequence历史并进入ONLINE。
- `boot_id=0`的ANNOUNCE、HEARTBEAT或TEARDOWN必须拒绝，不得刷新节点状态。
- 收到与当前会话`boot_id`一致的TEARDOWN后立即OFFLINE。
- 未知节点或`boot_id`不匹配的TEARDOWN必须忽略，不得改变当前节点状态。
- `boot_id`不匹配的HEARTBEAT必须忽略，等待新会话ANNOUNCE。
- OFFLINE表项可以被新的ANNOUNCE复用；优先复用空表项，其次复用最早进入OFFLINE的表项。
- 节点进入OFFLINE时清除该Source DevID的全部RX Sequence历史；后续合法ANNOUNCE作为新的接收基线。

### 10.2 时间参数

| 参数 | 默认值 |
|------|--------|
| ANNOUNCE 启动发送 | Topic注册完成并调用`umca_start()`后立即发送 |
| ANNOUNCE 周期重发 | 30 秒 |
| HEARTBEAT 周期 | 5 秒 |
| OFFLINE 超时 | 15 秒 |

所有超时判断必须使用无符号时间差，正确处理毫秒计数器回绕。

### 10.3 ANNOUNCE Payload

```text
capabilities  : u32
boot_id       : u32
topic_count   : u8
topic_entries : repeated TopicEntry
```

TopicEntry：

```text
topic_id      : u32
direction     : u8
name_length   : u8
name          : ASCII bytes[name_length]
```

| 字段 | 约束 |
|------|------|
| capabilities bit 0 | `CAP_PUBSUB` |
| capabilities bit 1~31 | 必须为 0 |
| boot_id | 非零；当前节点启动会话标识 |
| topic_count | 0~8 |
| direction bit 0 | Publisher |
| direction bit 1 | Subscriber |
| direction bit 2~7 | 必须为 0 |
| name_length | 1~63 |

完整 ANNOUNCE Payload 不得超过 256 字节。Topic 太多时只宣告比赛 Demo 所需的核心 Topic。

Payload长度为：

```text
9 + Σ(6 + name_length)
```

编码器不得静默截断已经提交的Topic列表；应用必须在启动前选择要宣告的核心Topic，超限时启动或编码返回明确错误。

### 10.4 HEARTBEAT Payload

```text
boot_id   : u32
uptime_ms : u32
status    : u8
```

`boot_id`必须与本节点ANNOUNCE中的值一致且非零。

HEARTBEAT Payload固定为9字节。

初版 `status`：

| 值 | 含义 |
|----|------|
| 0 | 正常 |
| 1 | 降级 |
| 2 | 故障 |

### 10.5 TEARDOWN Payload

```text
boot_id : u32
reason  : u8
```

TEARDOWN Payload固定为5字节。`boot_id`必须非零并与发送节点当前会话一致。

初版 `reason`：

| 值 | 含义 |
|----|------|
| 0 | 正常关闭 |
| 1 | 重启 |
| 2 | 维护模式 |
| 255 | 未指定 |

---

## 11. Pub/Sub

### 11.1 发布

发布接口逻辑语义：

```c
int umca_publish(
    umca_context_t *ctx,
    uint32_t topic_id,
    uint64_t destination,
    const uint8_t *payload,
    uint16_t payload_length);
```

发布过程必须：

1. 校验 Topic 是否已注册。
2. 校验 Payload 长度和当前 PHY MTU。
3. 获取该 Topic 的下一个 Sequence。
4. 编码完整帧并计算 CRC32C。
5. 通过 PHY Adapter 发送。
6. 返回明确状态码。

### 11.2 Topic注册

MVP在Context启动前一次性注册本地Topic：

```c
int umca_topic_register(umca_context_t *ctx,
                        const char *name,
                        uint8_t direction,
                        umca_topic_callback_t callback,
                        void *user_data,
                        uint32_t *topic_id_out);
```

- `direction`使用Publisher和Subscriber位组合。
- `direction`至少包含一个有效位，其他位必须为0。
- Publisher可以不提供callback，Subscriber必须提供callback。
- 注册时校验Topic名称并计算Topic ID。
- 同名重复注册可以合并方向；同ID不同名称返回`UMCA_ERR_TOPIC_COLLISION`。
- 重复Subscriber注册不得静默替换不同的callback或user_data。
- MVP不提供启动后的注册、取消订阅或注销接口。
- Topic 回调必须在协议任务上下文执行。
- 单个 Topic 的回调不得长期阻塞。
- 回调中需要执行复杂任务时，应复制必要数据并投递到业务队列。
- Payload 缓冲区只在回调期间有效，回调返回后不得继续引用。

### 11.3 本地路由

- 单播 DATA 只交付给 Destination DevID 与本节点相同的订阅者。
- 广播 DATA 交付给所有匹配订阅者。
- Source DevID 与本节点相同的回环帧默认忽略；测试配置可以显式允许。
- 未订阅 Topic 的合法帧可以统计，但不产生错误响应。

---

## 12. 比赛 Demo Topic 与消息

### 12.1 Topic 注册表

| Topic | Topic ID | Publisher | Subscriber | 状态 |
|-------|----------|-----------|------------|------|
| `/sensors/temperature` | `0x0FC95CB0` | Virtual Sensor | Agent | [Demo] |
| `/events/temperature/threshold` | `0x5389C486` | Virtual Sensor | Agent | [Demo] |
| `/actuators/fan/command` | `0xC72FE51A` | Agent | Virtual Actuator | [Demo] |
| `/actuators/fan/state` | `0x6D3F9EBE` | Virtual Actuator | Agent | [Demo] |

### 12.2 TemperatureSample

Topic：`/sensors/temperature`

| 偏移 | 字段 | 类型 | 单位 |
|------|------|------|------|
| 0 | `temperature_mc` | `i32` | 0.001℃ |
| 4 | `sample_time_ms` | `u32` | 节点启动后的毫秒数 |
| 8 | `quality` | `u8` | 0 无效，1 有效 |

Payload 固定 9 字节。例如 30.500℃编码为 `30500`。

### 12.3 TemperatureThresholdEvent

Topic：`/events/temperature/threshold`

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0 | `temperature_mc` | `i32` | 当前温度 |
| 4 | `threshold_mc` | `i32` | 触发阈值 |
| 8 | `direction` | `u8` | 0 低于阈值，1 高于阈值 |
| 9 | `event_time_ms` | `u32` | 事件时间 |

Payload 固定 13 字节。

### 12.4 FanCommand

Topic：`/actuators/fan/command`

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0 | `request_id` | `u32` | Agent生成的请求编号 |
| 4 | `command` | `u8` | 0关闭，1开启 |
| 5 | `source` | `u8` | 0应用，1 Agent，2安全策略 |

Payload 固定 6 字节。

### 12.5 FanState

Topic：`/actuators/fan/state`

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0 | `request_id` | `u32` | 对应 FanCommand |
| 4 | `state` | `u8` | 0关闭，1开启 |
| 5 | `result` | `u8` | 0成功，其他值失败 |
| 6 | `applied_time_ms` | `u32` | 实际执行时间 |

Payload 固定 10 字节。

---

## 13. 主动感知—决策—执行流程

### 13.1 被动查询

```text
用户询问当前温度
    ↓
ai_agent 调用 sensor_query
    ↓
适配层读取最近一次有效 TemperatureSample
    ↓
返回温度、时间、质量和节点在线状态
```

### 13.2 主动执行

```text
Virtual Sensor 发布 TemperatureSample
    ↓
温度超过配置阈值
    ↓
Virtual Sensor 发布 TemperatureThresholdEvent
    ↓
ai_agent 适配层提交主动事件
    ↓
Agent 决策并调用 device_control
    ↓
发布 FanCommand
    ↓
Virtual Actuator 执行并发布 FanState
    ↓
Agent 校验 request_id 和 result
```

### 13.3 防抖要求

- 阈值事件必须使用滞回区，避免温度在边界附近反复触发。
- 建议开启阈值 30.000℃，关闭阈值 28.000℃。
- 相同方向的阈值事件最短间隔默认 10 秒。
- Agent 不得在未收到 FanState 前宣称控制成功。

---

## 14. ai_agent 适配接口

### 14.1 分层约束

```text
ai_agent Tool/Skill
       ↓
ai_agent_adapter
       ↓
UMCA Pub/Sub API
       ↓
UMCA Core
```

UMCA Core 不得包含任何 ai_agent 头文件。

### 14.2 sensor_query

输入：

```json
{
  "sensor": "temperature"
}
```

输出语义：

```json
{
  "online": true,
  "valid": true,
  "temperature_mc": 30500,
  "age_ms": 120
}
```

当节点离线、数据无效或样本超时时，Tool 必须明确返回失败原因，不得伪造数据。

### 14.3 device_control

输入：

```json
{
  "device": "fan",
  "command": "on"
}
```

适配层行为：

1. 生成新的 `request_id`。
2. 发布 FanCommand。
3. 等待匹配 `request_id` 的 FanState。
4. 默认等待上限 1000ms。
5. 超时或 `result != 0` 时返回失败。

等待逻辑属于 ai_agent 适配层，不属于 UMCA Core 的可靠传输机制。

### 14.4 运行时语义补充（2026-08-22）

`umca_publish(ctx, topic_id, destination, payload, length)` 的第三个参数是目的
DevID；帧头中的 `source` 始终由 `ctx->local_dev_id` 自动填写。Demo 因此遵循：

- Agent 发布 FanCommand 时，目的为 Actuator DevID，source 自动为 Agent DevID；
- Actuator 发布 FanState 时，目的为 Agent DevID，source 自动为 Actuator DevID；
- `request_id` 是 Agent 适配层会话内的非零递增编号，回绕到 0 时跳到 1；
- Tool 只有在收到来源为 Actuator、`request_id` 匹配且 `result == 0` 的 FanState 后
  才能返回成功；默认等待上限为 1000ms；
- `result != 0`、来源不匹配、编号不匹配或超时均返回失败，不能把 UMCA DATA 发布
  成功误报为业务执行成功。

Actuator 必须在初始化时注册 `/actuators/fan/state` Publisher，否则 FanState 会被
UMCA Core 按本地 Topic 方向拒绝；该注册属于 Demo 应用约束，不改变 Core 协议。

### 14.5 Goldfish/ai_agent 验证状态

2026-08-22 已在 openvela Goldfish 的 Loopback 三节点服务中验证 Sensor → Agent →
Actuator → Agent 闭环，并通过 app-level ai_agent provider 实际执行 `sensor_query`
和 `device_control`。这不等同于 UART/GD32/ESP32 硬件互操作验证；未实现的分段、
可靠传输、RPC、ACL、加密、网关和多路径能力仍保持关闭。

---

## 15. PHY Adapter

### 15.1 接口

```c
typedef struct {
    int (*init)(void *driver);
    void (*deinit)(void *driver);
    int (*send)(void *driver, const uint8_t *data, size_t length);
    int (*poll)(void *driver,
                uint8_t *buffer,
                size_t capacity,
                size_t *received);
    size_t (*get_mtu)(void *driver);
    uint32_t (*get_capabilities)(void *driver);
} umca_phy_ops_t;

typedef struct {
    const umca_phy_ops_t *ops;
    void *driver;
} umca_phy_t;
```

- `umca_phy_ops_t`描述同一类PHY的操作。
- `umca_phy_t.driver`指向具体PHY实例的私有状态。
- Core必须通过`driver`调用所有PHY操作，不得依赖Adapter内部的可变全局状态。
- 同一进程必须能够创建多个使用相同ops、不同driver的PHY实例。
- `init/send/poll`成功返回`UMCA_OK`；平台私有错误必须由Adapter或Core映射为统一UMCA错误码。
- `poll`进入时必须先将`*received`置0；无完整帧可取时返回`UMCA_OK`且`received=0`。
- `poll`成功取出一帧时返回`UMCA_OK`且`received`为完整帧长度；不得返回半帧。
- `get_mtu`返回0表示实例未初始化或当前不可用；正常值必须至少为40。
- `get_capabilities`在没有已定义能力时返回0，未定义位必须为0。
- PHY Adapter默认必须抑制本实例发送产生的本地回显；只有显式测试配置可以把自发帧返回给同一Context。

### 15.2 缓冲区所有权

- `send(driver, ...)`返回后，调用者可以立即复用发送缓冲区。
- 异步 PHY 必须内部复制数据或维护引用计数。
- `poll(driver, ...)`使用调用者提供的缓冲区。
- Adapter 不得在 `poll()`返回后继续引用接收缓冲区。
- 初版 PHY 回调不得直接调用应用 Topic 回调。

### 15.3 Loopback PHY

- **[MVP]** 支持 2~4 个逻辑端点。
- **[MVP]** 保持发送顺序。
- **[MVP]** 使用固定深度队列。
- **[MVP]** 支持测试注入 CRC 错误、丢帧和重复帧。

**[未实现]** UART 实板硬件验证。

2026-09-06 起，UART PHY 的协议无关字节流组帧适配器已完成主机测试；“UART 硬件
验证”仍特指 GD32F470VKT6 实板上的真实串口电平、线缆、对端和连续运行验证，不能以
主机测试或 Goldfish Loopback 结果替代。Linux/Windows 工具链分工不属于线级协议：
Linux 生成固件，Windows 执行烧录和调试。

**[未实现]** CAN FD PHY。

**[未实现]** SPI PHY。

---

## 16. PAL

### 16.1 接口

```c
typedef struct {
    uint32_t (*time_ms)(void *user);
    uint64_t (*time_us)(void *user);
    uintptr_t (*critical_enter)(void *user);
    void (*critical_exit)(void *user, uintptr_t state);
    int (*mutex_lock)(void *user, void *mutex);
    int (*mutex_unlock)(void *user, void *mutex);
    void *(*alloc)(void *user, size_t size);
    void (*free)(void *user, void *ptr);
    void (*log)(void *user, int level, const char *message);
    void (*assert_fail)(void *user,
                        const char *file,
                        int line,
                        const char *expr);
} umca_platform_ops_t;

typedef struct {
    const umca_platform_ops_t *ops;
    void *user;
    void *mutex;
} umca_platform_t;
```

### 16.2 初版约束

- `UMCA_USE_DYNAMIC_MEMORY=0`时，`alloc/free`可以为 NULL，Core 不得调用。
- PAL操作必须通过`umca_platform_t.user`访问平台或测试实例状态，不得依赖可变全局状态。
- `time_ms(user)`必须单调递增，允许自然回绕。
- `time_us(user)`可选，不得用低精度Tick伪造微秒精度。
- `critical_enter(user)`必须返回进入前的平台状态。
- `critical_exit(user, state)`必须恢复原状态，不得无条件开启中断。
- mutex由宿主平台创建并通过`umca_platform_t.mutex`注入，Core不创建或销毁mutex。
- 未启用线程安全时`mutex`及`mutex_lock/mutex_unlock`可以为NULL。
- mutex 与关中断临界区必须分离。

---

## 17. 错误码

| 值 | 名称 | 含义 |
|----|------|------|
| 0 | `UMCA_OK` | 成功 |
| -1 | `UMCA_ERR_INVALID_ARG` | 参数无效 |
| -2 | `UMCA_ERR_BAD_MAGIC` | Magic错误 |
| -3 | `UMCA_ERR_UNSUPPORTED_VERSION` | 不支持的协议版本 |
| -4 | `UMCA_ERR_BAD_HEADER` | 帧头字段非法 |
| -5 | `UMCA_ERR_PAYLOAD_TOO_LARGE` | Payload或PHY MTU超限 |
| -6 | `UMCA_ERR_LENGTH_MISMATCH` | 实际长度与声明不一致 |
| -7 | `UMCA_ERR_CRC` | CRC校验失败 |
| -8 | `UMCA_ERR_UNSUPPORTED_TYPE` | 消息类型不支持 |
| -9 | `UMCA_ERR_CAPACITY` | 固定容量表或队列已满 |
| -10 | `UMCA_ERR_TOPIC_COLLISION` | Topic ID碰撞 |
| -11 | `UMCA_ERR_DUPLICATE` | 重复帧 |
| -12 | `UMCA_ERR_STALE` | 过期帧 |
| -13 | `UMCA_ERR_NOT_FOUND` | 节点或Topic不存在 |
| -14 | `UMCA_ERR_TIMEOUT` | 操作超时 |
| -15 | `UMCA_ERR_PHY` | PHY操作失败 |
| -16 | `UMCA_ERR_UNSUPPORTED_FLAGS` | Flags包含MVP不支持的位 |
| -17 | `UMCA_ERR_UNSUPPORTED_QOS` | QoS不是0 |
| -18 | `UMCA_ERR_BAD_PAYLOAD` | Payload字段或消息语义非法 |
| -19 | `UMCA_ERR_BAD_STATE` | Context生命周期状态不允许当前操作 |

公开 API 必须返回统一错误码，不得直接暴露平台私有错误值。

---

## 18. 编译配置

| 宏 | 初版值 |
|----|--------|
| `UMCA_USE_DYNAMIC_MEMORY` | 0 |
| `UMCA_ENABLE_PUBSUB` | 1 |
| `UMCA_ENABLE_DISCOVERY` | 1 |
| `UMCA_ENABLE_RX_SEQUENCE_TRACKING` | 1 |
| `UMCA_ENABLE_REMOTE_TOPIC_NAMES` | 1 |
| `UMCA_ENABLE_DEBUG` | 0 |
| `UMCA_ENABLE_FRAGMENTATION` | 0 |
| `UMCA_ENABLE_RPC` | 0 |
| `UMCA_ENABLE_CHANNEL_BONDING` | 0 |
| `UMCA_ENABLE_ACL` | 0 |
| `UMCA_ENABLE_ENCRYPTION` | 0 |
| `UMCA_ENABLE_QOS1` | 0 |
| `UMCA_ENABLE_QOS2` | 0 |
| `UMCA_ENABLE_QOS3` | 0 |

---

## 19. 接收处理流程

```text
PHY poll
   ↓
最小长度检查
   ↓
Magic / Version / Header Length
   ↓
Flags=0 / QoS=0 / Reserved=0 / Payload Length
   ↓
实际长度匹配
   ↓
CRC32C
   ↓
DevID / Message Type / Topic ID
   ↓
若为服务发现消息，完整校验Payload和boot_id
   ↓
Sequence去重与丢包统计
   ↓
原子提交服务发现会话、Sequence和节点状态，或提交DATA Sequence后执行Topic路由
   ↓
应用回调
```

任一步失败均终止当前帧处理。

---

## 20. 固定符合性测试向量

以下向量是协议冻结的一部分。所有十六进制字节按发送顺序排列，多字节整数均为大端。

### 20.1 CRC32C标准向量

```text
ASCII输入：123456789
CRC32C：0xE3069283
```

### 20.2 Topic ID向量

| Topic | FNV-1a 32位结果 |
|-------|-----------------|
| `/sensors/temperature` | `0x0FC95CB0` |
| `/events/temperature/threshold` | `0x5389C486` |
| `/actuators/fan/command` | `0xC72FE51A` |
| `/actuators/fan/state` | `0x6D3F9EBE` |

### 20.3 空Payload DATA帧

参数：广播、Source DevID=`0x1001`、Topic=`0x0FC95CB0`、Sequence=1。

```text
55 4d 01 24 00 00 00 00 08 00 ff ff ff ff ff ff ff ff
00 00 00 00 00 00 10 01 0f c9 5c b0 00 00 00 01 00 00
c8 c2 08 2a
```

帧长40字节，CRC32C=`0xC8C2082A`。

### 20.4 TemperatureSample DATA帧

参数：Destination=`0x2001`、Source=`0x1001`、Topic=`0x0FC95CB0`、Sequence=1；Payload表示30.500℃、采样时间1000ms、质量有效。

```text
55 4d 01 24 00 00 00 00 08 00 00 00 00 00 00 00 20 01
00 00 00 00 00 00 10 01 0f c9 5c b0 00 00 00 01 00 09
00 00 77 24 00 00 03 e8 01 f7 26 e5 28
```

帧长49字节，CRC32C=`0xF726E528`。

### 20.5 ANNOUNCE帧

参数：Source=`0x1001`、Boot ID=`0x10010001`，宣告一个Publisher Topic `/sensors/temperature`。

```text
55 4d 01 24 00 00 03 00 08 00 ff ff ff ff ff ff ff ff
00 00 00 00 00 00 10 01 00 00 00 00 00 00 00 01 00 23
00 00 00 01 10 01 00 01 01 0f c9 5c b0 01 14 2f 73 65
6e 73 6f 72 73 2f 74 65 6d 70 65 72 61 74 75 72 65 86
53 d2 17
```

帧长75字节，Payload长35字节，CRC32C=`0x8653D217`。

### 20.6 HEARTBEAT帧

参数：Source=`0x1001`、Boot ID=`0x10010001`、uptime=5000ms、status=0、Sequence=2。

```text
55 4d 01 24 00 00 04 00 08 00 ff ff ff ff ff ff ff ff
00 00 00 00 00 00 10 01 00 00 00 00 00 00 00 02 00 09
10 01 00 01 00 00 13 88 00 b9 b9 ab 67
```

帧长49字节，CRC32C=`0xB9B9AB67`。

### 20.7 TEARDOWN帧

参数：Source=`0x1001`、Boot ID=`0x10010001`、reason=0、Sequence=3。

```text
55 4d 01 24 00 00 05 00 08 00 ff ff ff ff ff ff ff ff
00 00 00 00 00 00 10 01 00 00 00 00 00 00 00 03 00 05
10 01 00 01 00 2b ea d1 35
```

帧长45字节，CRC32C=`0x2BEAD135`。

---

## 21. 必测用例

### 21.1 帧编解码

- 最小空 Payload 帧。
- 256 字节最大 Payload 帧。
- Magic错误。
- 未知 Version。
- Header Length错误。
- Reserved非0。
- 任意非零Flags拒绝。
- QoS 1~3拒绝且不得交付。
- Destination DevID为0拒绝。
- REQUEST、RESPONSE和ERROR类型拒绝。
- Payload Length超限。
- 声明长度与实际长度不一致。
- CRC错误。
- 大端编解码一致性。

### 21.2 Topic

- Topic规范化成功与失败。
- FNV-1a固定测试向量。
- Topic ID碰撞模拟。
- 未订阅 Topic 不触发回调。
- Payload生命周期只在回调期间有效。

### 21.3 Sequence

- 新会话首次发送为1。
- PHY发送失败不消耗Sequence。
- 正常递增。
- 重复帧。
- 跳号统计。
- 旧帧丢弃。
- `0xFFFFFFFF → 0x00000000`回绕。
- 同一DevID更换`boot_id`后清除旧Sequence历史。
- 节点快速重启后先ANNOUNCE，再接受新会话DATA。

### 21.4 服务发现

- ANNOUNCE上线。
- HEARTBEAT刷新状态。
- 15秒超时离线。
- TEARDOWN立即离线。
- TEARDOWN Boot ID不匹配时忽略。
- DevID冲突。
- 节点表满。
- OFFLINE表项按确定性策略复用。
- ANNOUNCE超过256字节拒绝。
- ANNOUNCE、HEARTBEAT和TEARDOWN的`boot_id`一致。
- `boot_id=0`拒绝。
- 同一DevID的ANNOUNCE携带新`boot_id`时触发会话更新。

### 21.5 比赛闭环

- 温度查询成功。
- 传感器离线时查询失败。
- 温度高于阈值触发事件。
- Agent发布风扇开启命令。
- 执行器返回匹配 request_id 的状态。
- 执行器超时后 Agent 不得宣称成功。
- 滞回区防止频繁开关。

---

## 22. 未实现能力

- **[未实现]** 分段与重组。
- **[未实现]** 可靠传输和自动重传。
- **[未实现]** RPC。
- **[未实现]** ACK、ACK_REQUIRED和ERROR消息。
- **[未实现]** ParameterService。
- **[未实现]** Broker模式。
- **[未实现]** 网关和跨域路由。
- **[未实现]** 透明桥接。
- **[未实现]** 多路径和Channel Bonding。
- **[未实现]** QoS-1、QoS-2、QoS-3策略。
- **[未实现]** DebugService。
- **[未实现]** ACL。
- **[未实现]** 加密和认证。
- **[未实现]** OTA和流式传输。
- **[未实现]** PC可视化调试工具。

---

## 23. 初版验收标准

1. UMCA Core可在POSIX/Linux单元测试中独立编译，不依赖openvela头文件。
2. openvela模拟器内至少三个逻辑节点通过Loopback互相发现。
3. TemperatureSample能够从传感器节点到达Agent节点。
4. `sensor_query`返回真实缓存数据和在线状态。
5. TemperatureThresholdEvent能够触发Agent主动任务。
6. `device_control`发布FanCommand并等待FanState确认。
7. CRC、长度、版本、容量和Topic碰撞错误均有测试。
8. UMCA运行期间不执行动态内存分配。
9. 编译日志、运行步骤和演示流程可以由 README 复现。
10. 同一进程可通过`ops + driver`创建至少三个独立Loopback PHY实例。
11. 节点更换`boot_id`后能够立即建立新会话并接收新的Sequence。
12. CRC、Topic和完整帧编码结果与第20节固定向量逐字节一致。
13. 非零Flags、非零QoS和未实现消息类型不会进入Topic回调。

---

*本文档是UMCA v0.3.3架构在openvela AI硬件比赛中的初版实现协议v0.1.2。本次修订在不改变固定帧头和协议版本`0x01`的前提下，冻结了严格的MVP Flags/QoS行为、三类服务发现消息的Boot ID会话语义、可移植Topic字符集、统一软件接口和固定符合性测试向量。未实现能力仅保留状态标记，不构成本版本的互操作承诺。*
