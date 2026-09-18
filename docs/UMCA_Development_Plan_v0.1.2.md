# UMCA 开发指导计划

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档名称 | UMCA 开发指导计划 |
| 文档版本 | v0.1.2 |
| 日期 | 2026-08-22 |
| 架构基线 | `UMCA_Architecture_Design_v0.3.3.md` |
| 协议基线 | `UMCA_Protocol_Specification_v0.1.2.md` |
| 目标项目 | openvela-UMC-Agent |
| 首发平台 | openvela Goldfish ARM64 模拟器 |
| 目标硬件 | GD32F470V-START（MCU：GD32F470VKT6） |
| 文档状态 | 正式开发指导冻结候选基线 |

### v0.1.2修订摘要

- PHY统一为`ops + driver`多实例描述符。
- PAL统一为`ops + user + mutex`宿主注入模型。
- ANNOUNCE、HEARTBEAT和TEARDOWN加入非零32位Boot ID。
- 节点Boot ID变化时清除对应Sequence历史并建立新会话。
- MVP严格要求Flags和QoS均为0，不执行静默降级。
- Topic注册API与软件详细设计统一，MVP不提供运行期取消订阅。
- Topic冻结为ASCII子集，增加节点表复用、PHY返回值和固定测试向量要求。
- 固定帧头、Profile范围和阶段路线保持不变。

---

## 1. 文档目的

本文档用于指导 UMCA 初版协议栈及 openvela 比赛项目的实际开发，明确：

- UMCA Core、平台适配、PHY Adapter、业务应用和 ai_agent 之间的边界。
- 无硬件阶段如何在 Linux 主机和 Goldfish 模拟器中推进开发。
- 硬件到位后如何迁移到 GD32F470V-START，并接入 ESP32 WiFi 透传模块。
- 后续裸机、FreeRTOS、Arduino 等节点如何复用同一协议栈。
- 如何通过编译期裁剪、静态资源配置和量化测试保证 UMCA 的轻量化。
- 每个阶段的输入、任务、交付物、测试要求和退出条件。

本文档是开发计划，不替代线级协议规范。任何影响帧格式、字段语义、字节序、CRC、Topic ID 或互操作行为的修改，必须先更新协议文档，再修改实现。

---

## 2. 项目目标与范围

### 2.1 总体目标

在 openvela 上实现轻量级、可裁剪、平台无关的 UMCA 通信中间件，通过统一 Topic 协议完成多节点发现与通信，并与 ai_agent 集成，实现：

```text
主动感知 → 智能决策 → 实时执行 → 状态反馈
```

### 2.2 当前比赛 MVP 范围

必须实现：

- 固定 UMCA 帧的编码、解码和严格校验。
- CRC32C 完整性校验。
- Topic 规范化及 FNV-1a 32 位 Topic ID。
- QoS-0 Broker-less Pub/Sub。
- Sequence 递增、去重、旧帧过滤和丢包统计。
- ANNOUNCE、HEARTBEAT、TEARDOWN 服务发现。
- 固定容量节点表、Topic 表和收发队列。
- POSIX PAL、openvela PAL 和 Loopback PHY。
- Goldfish 内三个逻辑节点的完整通信闭环。
- `sensor_query` 和 `device_control` 两个 ai_agent Tool/Skill。
- 编译、运行、测试和演示的可复现文档。
- MVP只接受`Flags=0`和`QoS=0`，不得把未实现能力静默降级。
- Topic使用协议规定的可移植ASCII子集，不执行Unicode规范化。

### 2.3 当前明确不实现

- 分段与重组。
- 可靠传输和自动重传。
- ACK、ACK_REQUIRED和通用ERROR消息。
- RPC 和 ParameterService。
- Broker 模式。
- 网关、透明桥接和跨域路由。
- 多路径和 Channel Bonding。
- QoS-1、QoS-2、QoS-3。
- DebugService。
- ACL、认证和加密。
- OTA、流式传输和 PC 可视化调试器。

这些能力仅保留架构位置或状态标记，不得以“为未来预留”为由增加 MVP 的运行时复杂度和资源占用。

---

## 3. 核心工程原则

### 3.1 轻量化是设计约束

轻量化不是开发完成后的优化工作，而是每个模块进入主线实现前的准入条件。

要求：

1. MVP 禁止运行期动态内存分配。
2. 未启用功能不得占用代码空间、静态 RAM、线程、队列、锁或定时器。
3. Core 不创建线程，调度方式由宿主平台决定。
4. Core 不直接输出格式化日志。
5. 所有容量均可在编译期配置。
6. 每种 Profile 必须生成独立 ROM/RAM 报告。
7. 使用链接器垃圾回收验证未引用模块已被移除。
8. 不为尚未进入开发阶段的功能设计复杂抽象。

### 3.2 可裁剪必须落实到源文件级

优先通过构建系统选择参与编译的源文件：

```text
启用服务发现：编译 discovery/*.c
关闭服务发现：discovery/*.c 不进入目标文件
```

不推荐将所有功能写入一个大文件，再通过运行时条件关闭。局部条件编译可以用于字段、容量和少量平台差异，但不得形成难以维护的宏分支网络。

### 3.3 Core 与平台完全分离

UMCA Core 不得直接依赖：

- openvela 或 NuttX 头文件。
- POSIX 头文件。
- FreeRTOS、Arduino 或具体厂商 SDK。
- UART、CAN、SPI、WiFi 驱动。
- ai_agent 头文件。
- 具体传感器或执行器业务模型。

依赖方向只能是平台和应用依赖 UMCA，不允许 UMCA 反向依赖平台或应用。

### 3.4 多实例而非全局单例

Core 必须支持多个独立 `umca_context_t` 实例，因为 Goldfish MVP 需要在一个进程内模拟传感器、Agent 和执行器三个逻辑节点。

除只读常量表外，禁止使用隐藏的可变全局状态。

### 3.5 错误必须显式传播

- 不允许静默截断 Payload。
- 容量不足必须返回 `UMCA_ERR_CAPACITY`。
- PHY 失败必须转换为统一 UMCA 错误码。
- CRC、版本、长度或保留字段错误的帧不得进入业务回调。
- 节点离线、数据过期和执行超时不得被应用层报告为成功。

---

## 4. 仓库与源码边界

### 4.1 比赛仓库

比赛仓库只保存 GD32/openvela 比赛项目及其开发、测试和复现所必需的内容。

推荐逻辑结构：

```text
contest2026_417_duwen/
├── umca/                         # 独立 UMCA 源码目录
├── app/
│   └── openvela_umca_agent/      # openvela应用和ai_agent适配
├── configs/                      # 可复现的openvela配置
├── scripts/                      # 安装、构建、运行和资源分析脚本
├── tests/                        # 比赛项目级集成与演示测试
├── docs/                         # 开发、移植、演示和测试文档
└── README.md
```

约束：

- `umca/` 必须是独立目录，不与 ai_agent 或 Demo 源码混合。
- Goldfish 支持代码属于 GD32 项目的前期验证设施，可以保存在比赛仓。
- 其他真实节点的板级代码不得进入比赛仓。
- 不直接提交本地 `vendor/openvela` 工作区中的临时修改；应保存补丁、配置片段或安装脚本以便复现。

### 4.2 UMCA 目录

推荐结构：

```text
umca/
├── include/
│   └── umca/
│       ├── umca.h
│       ├── umca_config.h
│       ├── umca_types.h
│       ├── umca_error.h
│       ├── umca_platform.h
│       └── umca_phy.h
├── src/
│   ├── base/
│   │   ├── umca_frame.c
│   │   ├── umca_crc32c.c
│   │   └── umca_topic_id.c
│   ├── core/
│   │   └── umca_context.c
│   ├── pubsub/
│   │   ├── umca_publisher.c
│   │   ├── umca_subscription.c
│   │   └── umca_router.c
│   ├── sequence/
│   │   └── umca_sequence.c
│   ├── discovery/
│   │   ├── umca_discovery.c
│   │   └── umca_node_table.c
│   └── diagnostics/
│       └── umca_stats.c
├── ports/
│   ├── posix/
│   └── openvela/
├── phy/
│   └── loopback/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── vectors/
│   └── fuzz/
├── cmake/
├── CMakeLists.txt
├── Kconfig
├── README.md
└── LICENSE
```

`ports/` 和 `phy/` 属于 UMCA 的参考适配实现，但不能被 `src/` 反向包含。

### 4.3 其他平台节点仓库

硬件可用后，其他节点分别建立独立仓库，例如：

```text
umca-node-baremetal/
umca-node-freertos/
umca-node-arduino/
```

每个节点仓库只保存自身的：

- PAL实现。
- PHY Adapter。
- 板级驱动。
- Topic和消息定义。
- 业务逻辑。
- 构建配置。
- 互操作测试记录。

比赛阶段以比赛仓的 `umca/` 为唯一源码真源。至少完成第二个平台的线级互操作验证后，再评估是否建立正式独立的 UMCA Core 仓库。

---

## 5. 总体依赖架构

```text
┌─────────────────────────────────────────────┐
│ ai_agent Skill / Tool                      │
│ sensor_query / device_control              │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│ openvela_umca_agent                         │
│ Agent适配、消息缓存、请求等待、Demo业务      │
└──────────────────────┬──────────────────────┘
                       │ UMCA Public API
┌──────────────────────▼──────────────────────┐
│ UMCA Core                                  │
│ Frame / PubSub / Discovery / Sequence      │
└──────────────┬────────────────┬─────────────┘
               │                │
┌──────────────▼───────┐ ┌──────▼────────────┐
│ PAL                  │ │ PHY Adapter       │
│ time/critical/mutex  │ │ loopback/UART/... │
└──────────────┬───────┘ └──────┬────────────┘
               │                │
┌──────────────▼────────────────▼─────────────┐
│ openvela / POSIX / 裸机 / FreeRTOS / Arduino│
└─────────────────────────────────────────────┘
```

### 5.1 ai_agent 适配层职责

- 将 Tool 参数转换为 UMCA Topic 消息。
- 缓存最新传感器样本及时间戳。
- 根据 `request_id` 匹配执行器反馈。
- 管理 Tool 等待和超时。
- 将主动阈值事件转换为 Agent 任务。

这些逻辑不属于 UMCA 的可靠传输、RPC 或服务发现能力。

### 5.2 Core 职责

- 帧编解码和校验。
- Topic注册、订阅和本地分发。
- Sequence生成和检查。
- 节点状态和服务发现。
- 调用PAL获得时间和同步能力。
- 调用PHY发送和接收完整UMCA帧。

### 5.3 Core 不负责

- 创建平台线程。
- 决定应用任务优先级。
- 等待LLM响应。
- 控制具体硬件。
- 保存配置文件。
- 提供网络Socket或UART驱动。

---

## 6. Profile与编译期裁剪

### 6.1 `UMCA_PROFILE_MINIMAL`

目标：资源受限的裸机传感器或执行器。

该Profile用于静态、受控链路，不属于比赛MVP完整发现Profile。它不提供零配置发现或基于Boot ID的热重启恢复；需要这些能力的节点必须使用Discovery Profile。

包含：

- 帧编码和解码。
- CRC32C。
- Topic ID计算。
- QoS-0 DATA发送和接收。
- 单PHY实例。
- 静态内存。

默认关闭：

- 服务发现。
- 节点表。
- Topic名称宣告。
- Sequence历史表。
- 运行统计。
- 日志。
- mutex。

### 6.2 `UMCA_PROFILE_DISCOVERY`

目标：普通传感器、执行器和多节点网络。

在 Minimal 基础上增加：

- ANNOUNCE、HEARTBEAT、TEARDOWN。
- 固定容量节点表。
- Sequence去重和丢包统计。
- 可选Topic字符串宣告。
- 可选基础运行统计。

### 6.3 `UMCA_PROFILE_CONTEST`

目标：openvela比赛Demo。

在 Discovery 基础上增加：

- openvela PAL。
- Goldfish阶段的Loopback PHY。
- 硬件阶段的UART PHY。
- 比赛Topic和消息定义。
- NSH诊断入口。
- ai_agent独立适配层。

`PROFILE_CONTEST`不是一个包含应用代码的UMCA库Profile。它是UMCA配置与比赛应用配置的组合名称。

### 6.4 功能配置

```c
#define UMCA_ENABLE_PUBSUB             1
#define UMCA_ENABLE_DISCOVERY          1
#define UMCA_ENABLE_RX_SEQUENCE_TRACKING  1
#define UMCA_ENABLE_REMOTE_TOPIC_NAMES    1
#define UMCA_ENABLE_STATS              1
#define UMCA_ENABLE_LOG                0
#define UMCA_ENABLE_ASSERT             1
#define UMCA_ENABLE_THREAD_SAFE        1
```

后续功能默认关闭：

```c
#define UMCA_ENABLE_FRAGMENTATION      0
#define UMCA_ENABLE_RPC                0
#define UMCA_ENABLE_QOS1               0
#define UMCA_ENABLE_QOS2               0
#define UMCA_ENABLE_QOS3               0
#define UMCA_ENABLE_DEBUG              0
#define UMCA_ENABLE_ACL                0
#define UMCA_ENABLE_ENCRYPTION         0
#define UMCA_ENABLE_CHANNEL_BONDING    0
```

### 6.5 容量配置

```c
#define UMCA_MAX_PAYLOAD          256
#define UMCA_MAX_NODES            8
#define UMCA_MAX_LOCAL_TOPICS     16
#define UMCA_MAX_TOPICS_PER_NODE  8
#define UMCA_RX_QUEUE_DEPTH       4
#define UMCA_TX_QUEUE_DEPTH       4
#define UMCA_MAX_TOPIC_NAME       64
```

Minimal Profile应使用更小容量，例如：

```c
#define UMCA_MAX_NODES            0
#define UMCA_MAX_LOCAL_TOPICS     4
#define UMCA_RX_QUEUE_DEPTH       1
#define UMCA_TX_QUEUE_DEPTH       1
```

### 6.6 平台能力配置

```c
#define UMCA_USE_DYNAMIC_MEMORY  0
#define UMCA_USE_MUTEX           0或1
#define UMCA_USE_TIME_US         0
```

### 6.7 配置依赖检查

编译期必须检查：

- 启用Discovery时，`UMCA_MAX_NODES`必须大于0。
- 启用Discovery时必须启用远端Topic名称缓存，以执行协议要求的碰撞检测。
- 启用Topic名称宣告时，`UMCA_MAX_TOPIC_NAME`必须大于0。
- `UMCA_MAX_PAYLOAD`不得超过协议Profile允许值。
- 队列深度不得为0，除非该方向明确使用同步直传。
- 启用线程安全时，PAL必须提供mutex或等价同步接口。
- 动态内存关闭时，Core不得引用`alloc/free`。
- 未实现功能宏被置1时，构建必须失败，不得生成行为不完整的固件。

---

## 7. PAL与PHY的轻量化要求

### 7.1 PAL按能力拆分

基础能力：

```text
time_ms
critical_enter
critical_exit
```

可选能力：

```text
thread-safe profile：宿主创建mutex并提供mutex_lock/mutex_unlock
log profile：log
assert profile：assert_fail
dynamic profile：alloc/free
high-resolution profile：time_us
```

关闭对应能力时，平台无需提供无意义的空函数。

### 7.2 临界区规则

- 所有PAL回调通过`user`访问平台或测试实例状态。
- `critical_enter(user)`返回进入前的平台状态。
- `critical_exit(user, state)`恢复原状态。
- 裸机实现不得无条件重新开启中断。
- mutex由宿主创建并通过平台描述符注入，Core不创建或销毁mutex。
- 互斥锁和关中断临界区职责必须分离。
- Topic业务回调不得在UMCA内部锁持有期间执行。

### 7.3 PHY Adapter规则

- Core只处理完整UMCA帧，不直接处理UART字节流状态机。
- UART组帧、缓冲和错误恢复属于UART PHY Adapter。
- `send()`返回后，调用者可以立即复用发送缓冲区。
- 异步发送必须由Adapter复制数据或管理自己的静态缓冲区。
- `poll()`使用调用者提供的接收缓冲区。
- PHY不得直接调用应用Topic回调。
- PHY采用`ops + driver`描述符；所有操作通过driver访问具体实例，不使用可变全局状态。
- `poll`无帧时返回`UMCA_OK`且`received=0`；成功时只交付一帧完整数据。
- `get_mtu()`返回0表示PHY未初始化或不可用。
- PHY MTU小于完整UMCA帧时必须明确拒绝；MVP不执行分段。

---

## 8. 公共API与生命周期

初版公共API固定为：

```c
int umca_init(umca_context_t *ctx,
              uint64_t local_dev_id,
              uint32_t local_boot_id,
              const umca_platform_t *platform,
              const umca_phy_t *phy);

int umca_start(umca_context_t *ctx);
int umca_stop(umca_context_t *ctx, uint8_t teardown_reason);
void umca_deinit(umca_context_t *ctx);
int umca_poll(umca_context_t *ctx, uint16_t *processed_frames);
int umca_publish(umca_context_t *ctx,
                 uint32_t topic_id,
                 uint64_t destination,
                 const uint8_t *payload,
                 uint16_t payload_length);
int umca_topic_register(umca_context_t *ctx,
                        const char *name,
                        uint8_t direction,
                        umca_topic_callback_t callback,
                        void *user_data,
                        uint32_t *topic_id_out);
```

生命周期要求：

1. `umca_context_t`由调用者持有。
2. PAL、PHY描述符及其user/driver实例在Context生命周期内保持有效。
3. `umca_init()`不创建线程，也不分配堆内存。
4. `umca_poll()`完成一次有界工作，不得无限阻塞。
5. 应用可在独立线程、事件循环或主循环中调用`umca_poll()`。
6. 回调中的Payload只在回调执行期间有效。
7. `deinit()`必须可重复安全处理已停止实例。

公共API签名和语义以`UMCA_Software_Design_v0.1.2.md`为准。私有结构布局可以在不改变静态资源所有权、多实例能力、协议行为和公开ABI的前提下优化。

---

## 9. 分阶段开发路线

### 9.1 阶段0：协议冻结与测试向量

#### 输入

- 架构文档v0.3.3。
- 协议规范v0.1.2。

#### 任务

1. 复核36字节帧头和40字节固定开销。
2. 将协议规范第20节固定向量录入只读测试数据，不得由待测实现生成期望值。
3. 独立复核CRC32C、Topic ID及完整帧向量。
4. 补充256字节最大Payload边界向量。
5. 建立Sequence回绕测试向量。
6. 建立带Boot ID的ANNOUNCE、HEARTBEAT以及TEARDOWN样例字节流。
7. 明确每个字段的非法值和预期错误码。

#### 交付物

```text
umca/tests/vectors/frame_vectors.*
umca/tests/vectors/crc32c_vectors.*
umca/tests/vectors/topic_vectors.*
docs/protocol_conformance.md
```

#### 退出条件

- 所有向量都能由人工检查或独立脚本验证。
- 协议描述不存在依赖C结构体内存布局的内容。
- 编码阶段不再临时修改线级格式。

### 9.2 阶段1：平台无关Core

#### 任务顺序

1. 基础类型、错误码和配置检查。
2. 网络字节序读写辅助函数。
3. CRC32C。
4. 帧编码和解码。
5. Topic规范化和Topic ID。
6. 实例上下文和静态资源布局。
7. 订阅表和本地路由。
8. 发布接口和Sequence生成。
9. 接收Sequence检查。
10. 统计模块。

#### 开发规则

- 每完成一个模块先增加主机单元测试。
- 不引入openvela头文件验证“能编译”之后再测试。
- 测试不得只覆盖成功路径。
- 先实现同步、可预测的处理流程，再考虑性能优化。

#### 退出条件

- Core可在Linux主机独立构建。
- 帧、CRC、Topic、Sequence测试全部通过。
- Minimal Profile不链接Discovery和Stats。
- 运行期无堆分配。
- 多个Context互不污染。

### 9.3 阶段2：POSIX PAL与Loopback PHY

#### 任务

1. 实现单调毫秒时钟。
2. 实现可选mutex和临界区。
3. 实现2～4端点Loopback总线。
4. 实现固定深度帧队列。
5. 实现丢帧、重复帧、CRC破坏注入。
6. 实现ANNOUNCE、HEARTBEAT、TEARDOWN。
7. 实现节点上线、刷新、超时和立即离线。

#### 退出条件

- 三个逻辑节点可互相发现。
- 节点表满、队列满和Topic碰撞返回明确错误。
- OFFLINE节点表项可确定性复用，优先空表项，其次最早进入OFFLINE的表项。
- 15秒心跳超时行为符合协议。
- Loopback错误注入测试可重复。
- AddressSanitizer或等价主机检查无越界和生命周期错误。

### 9.4 阶段3：Goldfish/openvela集成

#### 定位

Goldfish用于验证openvela API、任务模型、NSH入口和ai_agent集成，不用于推断GD32的实际资源占用。

#### 任务

1. 实现openvela PAL。
2. 将UMCA作为独立库接入openvela构建。
3. 建立UMCA运行线程或事件循环入口。
4. 实现Virtual Sensor、Agent Node和Virtual Actuator。
5. 增加NSH管理和诊断命令。
6. 保存可复现配置和构建脚本。

#### 建议NSH命令

```text
umca start
umca stop
umca status
umca nodes
umca topics
umca stats
umca demo start
umca demo stop
```

#### 退出条件

- Goldfish中三个节点完成发现。**已完成（2026-08-22）**。
- 温度Topic能从Sensor到达Agent。**已完成**。
- FanCommand能到达Actuator并返回FanState。**已完成**。
- UMCA可独立启动和停止。**已完成**；服务任务使用独立 `task_create("umca_svc", ...)`，
  避免 builtin app 主入口返回后任务组回收服务线程。
- 编译及运行步骤能从干净环境复现。**已完成当前配置复现**；QuickApp 的缺失 LFS
  库和宿主 `libpulse.so.0` 属于环境准备限制，需按记录补齐后运行。

### 9.5 阶段4：ai_agent闭环

#### 任务

1. 建立`openvela_umca_agent`适配层。
2. 缓存最新TemperatureSample和时间戳。
3. 实现`sensor_query`。
4. 实现`device_control`。
5. 根据`request_id`等待FanState，默认超时1000ms。
6. 将TemperatureThresholdEvent转换为主动Agent任务。
7. 实现温度滞回，避免风扇频繁启停。
8. 增加不依赖LLM的本地规则兜底路径。

#### 退出条件

- 查询返回真实数据、在线状态和数据年龄。**已完成**。
- 传感器离线或数据过期时查询失败。**已完成**。
- 执行器未确认时Tool不得返回成功。**已完成**；仅接受匹配来源、`request_id` 且
  `result == 0` 的 FanState，默认超时 1000ms。
- LLM不可用时UMCA继续运行。**已完成**；本地规则路径不依赖 LLM。
- 本地规则可完成确定性闭环演示。**已完成**。
- LLM可用时可展示智能决策扩展。**已完成**；实体ESP32-S3调用MiMo云端API的C1文本
  回复和有限动作建议已于2026-09-18通过最终混合实物验收。

ai_agent 的实际接入位于 `app/openvela_umca_agent`：适配层使用弱引用保持 Core
独立，在 ai_agent 应用进入同一 NuttX 镜像后注册 `umca` provider，并失效工具缓存
以便运行时发现两个 Tool。可使用 `umca_agent tools|sensor_query|device_control on|off`
进行现场检查。

### 9.6 阶段5：GD32移植准备与资源门禁

#### 2026-09-06 硬件阶段同步

开发板已就绪，确认硬件型号为 GD32F470V-START，MCU 为 GD32F470VKT6。开发分工为：

- Linux：使用 openvela 交叉工具链生成 ELF、BIN、HEX 和 MAP；
- Windows：连接板载 GD-Link/CMSIS-DAP，执行烧录、复位、串口观察和 GDB 调试；
- `.vscode/`：本阶段不修改，也不把 IDE 配置作为构建依赖。

硬件接口现已冻结：USART0 使用 PB6/PB7、AF7、115200 8N1，作为 `/dev/ttyS0` NSH
控制台；UMCA 独占 UART3，使用 PC10/TX、PC11/RX、AF8、115200 8N1，对应
`/dev/ttyS1`。链路采用 3.3 V CMOS TTL、双方共地、TX/RX 交叉连接，不启用 RTS/CTS，
首轮联调不启用 DMA。PC10/PC11 与 SDIO_D2/SDIO_D3 复用，因此 UMCA 基线显式关闭
SDIO，后续不得在不重新评审引脚的情况下同时启用。PA9 和 PD2 分别接入板载 USB
VBUS 检测与电源控制网络，不再用于 UART。
同轮审计已将继承自 I-EVAL 且需要 GPIOF-I 的 SPI5、ENET、EXMC、TLI 和
DCI 旧映射改为编译期拒绝。200 MHz Profile 的 PLLQ 实际为 50 MHz，因此
当前时钟驱动下不启用 USB FS、SDIO 或 TRNG；需要这些外设时应改用
168 MHz 或 240 MHz Profile 并重新审查引脚。

上游 Kconfig 当前没有 `GD32F470VK` 芯片项，板级暂以 `GD32F470IK` 兼容项描述同系列
外设；目标实物和链接内存布局仍严格按 GD32F470VKT6/LQFP100 管理。

Linux 端的官方板级 Make 流程为：

```bash
cd /path/to/openvela/nuttx
export PATH=/path/to/openvela/prebuilts/tools/python/bin:\
/path/to/openvela/prebuilts/build-tools/linux-x86_64/bin:\
/path/to/openvela/prebuilts/tools/linux/x86_64:\
/path/to/openvela/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$PATH
export PYTHONPATH=/path/to/openvela/prebuilts/tools/python/dist-packages/kconfiglib
./tools/configure.sh -E -l \
  ../vendor/gigadevice/boards/gd32f4/gd32f470v_start/configs/nsh
make -j$(nproc)
```

当前环境中 `./build.sh ... --cmake` 可识别该板，但会因 `arch/arm/src/gd32f4`
缺少 CMake 构建入口而失败；这不是 UMCA 代码错误，因此 GD32 阶段固定采用上面的
Make/configure 路径，除非上游补齐该架构的 CMake 支持。

Linux 实测的最小 NSH 历史基线为 Flash 230,004 B、SRAM 6,700 B；2026-09-09
完成 UART3 验证及远端 Topic 状态修复并重新全量链接后，`umca_gd32` 固件为 Flash
244,656 B、SRAM 15,920 B。2026-09-08 的初始 UART3 构件 Flash 244,640 B 保留为
历史对照。
这些数据只用于当前配置的资源门禁，不替代后续实板栈峰值测量。

Windows 端建议使用板载 GD-Link 的 CMSIS-DAP 接口，通过 OpenOCD 或 SEGGER/J-Link
兼容工具完成烧录和 GDB 连接。烧录地址、复位方式和目标脚本必须以实际 Windows
工具链识别结果为准；Linux 端只负责生成固件，不在仓库内提交 Windows 用户目录、
驱动安装结果或 `.vscode/` 配置。

#### 无硬件阶段可完成

1. 确认GD32 openvela板级配置和工具链。
2. 交叉编译UMCA Core和openvela PAL。
3. 检查链接Map文件。
4. 分模块统计`.text/.rodata/.data/.bss`。
5. 定义UART PHY接口和静态缓冲区。
6. 定义GD32与ESP32之间的字节流封装。
7. 评估任务栈、队列和ai_agent资源。

#### 资源门禁

不得使用Goldfish的122MB ROM和126MB RAM区域数据判断GD32可行性。必须以GD32专用配置的链接结果为依据，并分别统计：

- openvela内核和驱动。
- UMCA Core。
- PAL。
- UART PHY。
- ai_agent。
- TLS、JSON和网络组件。
- 应用任务栈和静态缓冲区。

#### 退出条件

- GD32目标可以完成链接。
- UMCA自身资源满足预算或有明确裁剪方案。
- 固件总体不超过目标Code Flash和SRAM区域。
- 没有将Goldfish预编译库带入GD32目标。
- UART PHY 字节流适配器已完成主机分片、粘包和短写测试；2026-09-09 已在实板完成
  UART3 双向收发及 UMCA 基础互操作验收（ANNOUNCE、HEARTBEAT、CRC32C、帧长度、协议
  语义、节点上线、Sequence），错误码为 0 且无 HardFault。
- UART3/PC10/PC11、`/dev/ttyS1`、115200 8N1 和 SDIO 互斥约束已进入构建配置与文档。

### 9.7 阶段6：硬件到位后的GD32与ESP32验证

#### 任务

1. 验证系统启动、时钟和串口。
2. 实现并验证UART PHY Adapter。
3. ESP32-S3运行三串口固定路由网关和本地 UMCA LLM节点固件。
4. 建立 GD32、Sensor、Actuator三个串口域之间的帧路由，并接入MiMo HTTPS API。
5. 验证断线、重连、粘包、拆包和缓存溢出。
6. 进行连续运行和故障注入测试。
7. 测量真实ROM、RAM、栈峰值、吞吐量和延迟。

本阶段的第一条实板路径是“GD32 启动与串口可观测性”，然后才是 GD32↔ESP32 的
UMCA 帧互操作。烧录成功、NSH 可见和 UART 回显成功只能证明板级启动/串口链路，
不能直接证明 UMCA 帧已通过 CRC、Sequence 和 Topic 路由验证。

2026-09-09 起，硬件验证改为两阶段推进：第一阶段使用 Windows COM7 CH340 模拟
ESP32 侧，完成 GD32↔Windows 的原始 UMCA 帧互通，覆盖 ANNOUNCE、HEARTBEAT、DATA、
CRC、Sequence、半帧、粘包和断线恢复；第二阶段实现 Windows COM7↔TCP 字节透明网桥，
验证 UART↔网络双向透传及 TCP 分包/粘包。网桥不得解析或修改 UMCA 帧，使用固定容量
缓冲，断线期间不重放旧控制命令。该阶段计划在两阶段通过后替换为真实 ESP32 固件；
替换和最终混合实物功能验收已于2026-09-18完成。

第一阶段已于 2026-09-09 通过。完整测试增量为 `rx_frames=13`、`rx_crc_errors=1`、
`rx_duplicates=1`、`rx_stale=1`、`rx_unsubscribed=6`、`rx_semantic_errors=0`、
`node_online_events=1`、`node_offline_events=1`、`capacity_errors=0`；坏 CRC 负向用例使
粘滞 `last_error` 保持为 `UMCA_ERR_CRC(-7)`，符合设计。新 Boot ID 建立新会话，停止
心跳 15 秒后节点正常转为 OFFLINE。第二阶段 COM7↔TCP 实板验证基线已于
2026-09-14 通过。

同日，远端 Topic `used` 标志修复已通过 GD32 实机回归，`umca_node_get()` 逐字段
复制修复已通过主机回归；第二阶段 COM7↔TCP 实测前置条件全部满足。

第二阶段基线结果：TCP→UART `read=493, sent=493`；UART→TCP `read=245, sent=98,
offline_drop=147`；连接/断开各 2 次，两个方向均无溢出，串口清理失败为 0。GD32
接收 9 帧，未订阅 DATA 为 5，CRC、长度、语义、重复、陈旧和容量错误均为 0，最终
正常离线且无 HardFault。固定缓冲溢出、非空缓冲断线清空和旧数据不重放专项随后
通过，Windows 网桥阶段功能验收完成。当时后续工作中的真实 ESP32替换已于
2026-09-18完成，长期连续运行仍作为独立可靠性项目。

专项测试确认：断线时应用缓冲按约束清空，新连接不重放旧数据，缓冲溢出遵循“丢弃
新数据并计数”。已经进入 UART 驱动、FIFO 或硬件移位寄存器的字节无法撤回，极小
缓冲断线可能使 UMCA 接收端产生一次 CRC 错误并重新同步，属于预期硬件边界。Windows
GBK 下中文测试输出的编码异常只影响日志显示，不影响网桥逻辑。

Windows TCP 测试端的身份参数固定为 `source/dev_id=0x3300000000000001`；Boot ID 作为
独立 32 位会话参数从 `0x33000001` 切换为 `0x33000002`，不得拼接进 DevID。

首轮硬件接线固定为 GD32 PC10/UART3_TX→对端 RX、GD32 PC11/UART3_RX←对端 TX、
GND↔GND。对端必须是 3.3 V TTL；不得直接连接 RS-232 电平或 5 V TTL。

#### ESP32-S3最终边界（2026-09-14冻结）

最终器件为 ESP32-S3-N16R8。UART0保留烧录和日志，UART1连接 GD32，UART2连接
Actuator，RMT辅助软件UART连接低频 Sensor。ESP32负责：

- 三个串口入口的完整UMCA帧识别、CRC校验和固定容量队列。
- 按静态 DevID出口表转发，TTL减1并重新计算 CRC，禁止发回入口。
- 作为独立 UMCA节点处理 `/llm/chat/request`，通过 Wi-Fi/HTTPS调用MiMo API并发布
  `/llm/chat/response`。
- 网络、TLS和LLM任务与高优先级串口路由任务隔离。
- 不重放旧 LLM请求或控制命令，所有缓冲和并发数有固定上限。

ESP32不得直接授权物理动作。LLM只返回有限 Action Proposal，GD32校验来源、Boot ID、
request_id、动作白名单、参数范围和 Actuator在线状态后才发布 FanCommand。

此前通过的 COM↔TCP纯字节透明测试保留为链路基线，不再代表最终 ESP32固件规格。
Windows `umca_three_node_sim.py`可通过单一 COM模拟 ESP32、Sensor和Actuator，作为无
实体网关时的回归工具。实体ESP32-S3固定路由、Wi-Fi/TLS和MiMo云端API已于
2026-09-18完成验收；凭据仍不得写入仓库或固件源码。

#### 退出条件

- GD32与ESP32之间可稳定双向传输UMCA帧。
- 三个串口域的单播、广播、TTL、CRC和禁止回送均通过验证。
- LLM请求与响应按 `requester_boot_id + request_id`正确匹配。
- WiFi断线不会导致GD32缓冲区无限增长。
- CRC错误和半帧不会进入Topic回调。
- 真实资源与延迟数据已记录。

### 9.8 阶段7：其他节点平台化

#### 推荐顺序

1. 裸机或FreeRTOS节点。
2. Arduino/ESP32节点。
3. 第二种真实PHY。
4. 正式独立UMCA Core仓库评估。

#### 每个平台必须完成

- PAL能力映射表。
- PHY缓冲区所有权说明。
- 相同协议测试向量。
- 与GD32节点的互操作测试。
- ROM/RAM报告。
- 未实现能力列表。

#### 退出条件

- 至少两种不同OS模型的实现能够互操作。
- 不需要修改UMCA Core即可增加新平台。
- 如必须修改Core，应先判断是协议缺陷、抽象缺陷还是平台实现错误。

---

## 10. 测试体系

### 10.1 单元测试

| 模块 | 必测内容 |
|------|----------|
| CRC32C | 标准向量、空输入、分块一致性 |
| Frame | 最小帧、最大帧、字节序、错误长度、未知版本、CRC错误 |
| Topic | 规范化、非法名称、固定哈希向量、碰撞模拟 |
| Sequence | 正常递增、重复、旧帧、跳号、32位回绕、Boot ID变化后重置 |
| Node Table | 上线、刷新、超时、冲突、容量耗尽 |
| Pub/Sub | 启动前Topic注册、方向合并、未订阅Topic、回调生命周期 |
| Config | 非法宏组合必须构建失败 |

### 10.2 集成测试

- 三节点发现。
- Sensor到Agent数据传输。
- Agent到Actuator命令传输。
- Actuator到Agent状态反馈。
- 心跳超时。
- TEARDOWN立即离线。
- 队列满。
- CRC破坏。
- 丢帧、重复帧和旧帧。
- 节点更换Boot ID后立即建立新会话。
- 非零Flags、非零QoS和未实现消息类型必须拒绝。
- TEARDOWN Boot ID不匹配时不得注销新会话。

### 10.3 ai_agent测试

- `sensor_query`正常返回。
- 节点离线。
- 样本过期。
- `device_control`成功确认。
- `request_id`不匹配。
- 执行器拒绝。
- 执行器超时。
- LLM超时或不可用。
- 本地规则兜底。

### 10.4 互操作测试

每个新平台必须执行：

1. 读取固定二进制帧并解析。
2. 编码相同语义并与标准字节流比较。
3. 验证CRC32C完全一致。
4. 验证Topic ID完全一致。
5. 与GD32或POSIX参考节点交换ANNOUNCE和DATA。

### 10.5 故障测试

- UART半帧和粘包。
- PHY返回短写或失败。
- 接收队列溢出。
- 节点快速重启、更换Boot ID并重置Sequence。
- DevID冲突。
- Topic ID碰撞。
- 时间计数器回绕。
- 回调重入发布。

---

## 11. 资源预算与测量

### 11.1 初始预算

以下为开发目标，不是协议承诺：

| Profile | UMCA Flash目标 | UMCA静态RAM目标 |
|---------|---------------:|----------------:|
| Minimal | ≤16 KiB | ≤4 KiB |
| Discovery | ≤32 KiB | ≤12 KiB |
| Contest | ≤48 KiB | ≤16 KiB |

UMCA资源不得与ai_agent、TLS、网络栈和openvela内核合并后笼统报告。

### 11.2 必须记录的指标

- Core代码大小。
- 各可选模块代码大小。
- `.rodata/.data/.bss`。
- Context大小。
- 单节点表项大小。
- 单Topic表项大小。
- 单帧缓冲区大小。
- RX/TX队列总大小。
- 每个任务的栈配置和峰值。
- 单帧编解码耗时。
- 发布到回调的端到端延迟。
- UART和WiFi吞吐量。

### 11.3 Profile差异检查

构建系统应生成至少三份资源报告：

```text
umca-size-minimal.txt
umca-size-discovery.txt
umca-size-contest.txt
```

如果关闭某功能后其目标文件或符号仍存在，应视为裁剪失败。

---

## 12. 近期开发任务清单

按优先级执行：

### P0：开发基线

1. 在比赛仓建立独立`umca/`目录。
2. 建立公共头文件、源码目录和主机测试目录。
3. 建立Minimal、Discovery、Contest配置。
4. 建立CMake主机构建入口。
5. 将协议固定向量放入`tests/vectors/`。

### P1：基础协议

1. 错误码和配置检查。
2. 字节序辅助函数。
3. CRC32C。
4. Frame编码和解码。
5. Topic规范化及Topic ID。
6. 完成对应单元测试。

### P2：消息与多实例

1. Context和调用者持有的静态资源。
2. 订阅表。
3. 发布和本地路由。
4. Sequence生成与检查。
5. 多实例隔离测试。

### P3：发现与Loopback

1. POSIX PAL。
2. Loopback PHY。
3. 节点表。
4. ANNOUNCE、HEARTBEAT、TEARDOWN。
5. 三节点集成测试。

### P4：openvela和Demo

1. openvela PAL。**完成**。
2. openvela构建集成。**完成**。
3. NSH/应用诊断命令。**完成当前 `umca_agent` 命令集**；通用 `umca start/stop/...`
   管理命令仍是后续增强项。
4. 三个虚拟节点。**完成**。
5. Demo Topic闭环。**完成**。

### P5：ai_agent

1. `sensor_query`。**完成**。
2. `device_control`。**完成**。
3. 主动阈值事件。**完成**。
4. 本地规则兜底。**完成**。
5. 完整演示脚本。**尚未单独固化脚本**，当前由 NSH 命令和运行日志复现。

---

## 13. 阶段交付与退出门禁

| 阶段 | 关键交付物 | 退出门禁 |
|------|------------|----------|
| 0 协议冻结 | 测试向量、符合性说明 | 线级格式无歧义 |
| 1 Core | 基础库、单元测试 | 主机独立编译、无动态内存 |
| 2 Loopback | POSIX PAL、三节点测试 | 发现、收发、故障注入通过 |
| 3 Goldfish | openvela PAL、NSH、虚拟节点 | 模拟器完整闭环 |
| 4 ai_agent | 两个Tool/Skill、事件桥 | 查询、控制、超时语义正确 |
| 5 GD32准备 | 交叉编译、Map和资源报告 | 满足目标硬件资源约束 |
| 6 硬件验证 | UART/WiFi透传、实测报告 | 稳定互操作和故障恢复 |
| 7 多平台 | 裸机/RTOS/Arduino适配 | 第二平台无需修改Core |

任何阶段未满足退出门禁时，不应通过增加更多功能掩盖基础问题。

---

## 14. 风险与降级方案

| 风险 | 影响 | 降级或缓解方案 |
|------|------|----------------|
| GD32资源不足以运行完整ai_agent | 无法在目标板完成完整AI闭环 | 先保证UMCA和本地规则；裁剪ai_agent通道、工具和上下文；重新评估云端接口最小客户端 |
| 无硬件导致UART方案未验证 | 模拟器结果无法代表真实链路 | 明确状态标记；使用主机伪串口测试字节流；硬件到位后优先验证PHY |
| ESP32缓存管理不当 | 断线时丢帧或内存耗尽 | 固定深度队列、明确丢弃策略、链路状态Topic |
| 帧头开销对低带宽链路较大 | 小消息有效载荷比例低 | MVP保持统一格式；先测量，不提前引入压缩Profile |
| Topic ID碰撞 | 错误路由 | 服务发现同时宣告名称；检测碰撞后拒绝注册 |
| 宏配置过多 | 构建组合难以维护 | 以三个Profile提供经过测试的组合；高级宏只用于专家配置 |
| Core被openvela实现细节侵入 | 后续移植困难 | 主机独立构建作为持续门禁；禁止Core包含平台头文件 |
| LLM网络不稳定 | 比赛演示超时 | 本地规则兜底；UMCA通信与LLM生命周期解耦 |

---

## 15. 版本与变更管理

### 15.1 三类版本

| 类型 | 当前版本 | 作用 |
|------|----------|------|
| 架构版本 | v0.3.3 | 描述长期架构、MVP严格能力和实现边界 |
| 协议版本 | v0.1.2 | 定义当前线级互操作规则、严格MVP能力和Boot ID会话语义 |
| 实现版本 | 初始v0.1.0 | 标识UMCA源码发布状态 |

帧中的`Version=0x01`对应v0.1.x线级协议族；v0.1.2没有修改固定帧头，但冻结了服务发现Payload、严格Flags/QoS行为和软件接口，不直接对应架构文档版本或实现版本。

### 15.2 变更顺序

涉及线级行为：

```text
提出问题 → 更新协议规范 → 更新测试向量 → 更新Core → 更新各平台 → 互操作验证
```

只涉及平台实现：

```text
更新PAL/PHY → 平台测试 → 资源报告 → 不修改协议版本
```

只涉及应用Topic：

```text
更新应用消息定义 → 更新应用测试 → 不修改UMCA Core
```

---

## 16. 完成定义

比赛阶段UMCA初版完成需要同时满足：

1. `umca/`作为独立目录存在，Core不依赖openvela或ai_agent。
2. POSIX主机测试可独立构建和运行。
3. Minimal Profile能证明服务发现、日志和统计已被真正裁剪。
4. Goldfish中至少三个逻辑节点完成发现和Topic通信。
5. `sensor_query`返回真实传感器状态。
6. `device_control`等待执行器明确反馈。
7. 主动阈值事件能够触发决策和执行闭环。
8. 无LLM时存在可演示的本地规则路径。
9. CRC、长度、版本、容量、重复帧和Topic碰撞均有测试。
10. GD32目标具备可审查的交叉编译和资源报告。
11. 完成UART实板基线、实体ESP32-S3路由和MiMo云端API验收；Sensor允许由Windows
    串口模拟作为最终交付节点。
12. README能够指导第三方复现构建、运行和演示。

---

## 17. 当前结论

截至 2026-09-18，协议测试向量、平台无关 Core、POSIX 主机测试、Goldfish/openvela
三节点闭环、ai_agent Tool运行时接入，以及最终混合实物业务闭环均已完成当前MVP门禁。
最终闭环由实体GD32、实体ESP32-S3、实体Actuator和Windows串口Sensor组成。后续工作应
转向：

1. 固化可重复的 Goldfish 构建/启动脚本，并补齐通用 `umca` 生命周期诊断命令；
2. 归档实体ESP32-S3路由、MiMo云端API和Windows Sensor的可重复验收证据；
3. 在硬件验证前保持分段、可靠传输、RPC、ACL、加密、网关和多路径等能力关闭。

Goldfish 使用 Loopback PHY，仅证明三节点逻辑闭环和 openvela 任务模型；不得将模拟器
资源数据外推为 GD32 结论。UMCA Core 仍保持不依赖 openvela、POSIX 或 ai_agent。
2026-09-14的GD32业务验收使用Windows模拟三个远端角色，属于历史阶段；2026-09-18
已由实体ESP32-S3和实体Actuator替换对应Mock，Sensor则正式保留为Windows串口模拟。
真实MiMo云端API已完成验收。

比赛期间，`contest2026_417_duwen/umca/`作为UMCA源码唯一真源；裸机、FreeRTOS和Arduino节点使用独立仓库，并在协议和Core稳定后开始开发。至少完成第二个平台互操作验证后，再决定是否将UMCA拆分为正式独立仓库。

所有开发阶段都必须同时回答两个问题：

1. 当前功能是否满足协议与Demo需求？
2. 关闭该功能后，其代码和静态资源是否真正消失？

只有两者都满足，才能认为UMCA实现了“轻量级、可裁剪、跨平台”的设计目标。

---

## 18. 最终GD32功能冻结状态（2026-09-17）

GD32最终Agent功能已实现、冻结并通过验收：

1. `sensor_query`和`device_control`已通过真实`tool_registry` external provider API注册；
2. `chat`在V1文本字段内注入`LlmChatRequest V1 Compact Context C1`可信快照；
3. 非零LLM status明确计数并输出失败，不显示空成功回复、不执行动作；
4. LLM动作经过Source、Boot ID、request_id、deadline和白名单校验后，再调用
   `device_control`等待真实FanState；
5. 新增默认关闭的`agent-auto on|off|status`，与本地auto互斥、容量1、60秒冷却；
6. Windows Mock已严格解析C1并支持可信温度问答。

已完成MINIMAL 3/3、DISCOVERY 5/5、CONTEST 5/5 CTest和Windows 14/14测试，Python
语法检查及GD32完整交叉构建通过。最终镜像Flash 262,572 B、SRAM 17,668 B。最终固件
Windows烧录、实体ESP32-S3 Provider、实体Actuator、Windows串口Sensor和MiMo云端API
均已完成闭环验收。后续只补充长期运行和故障注入证据；除阻断性缺陷外，不再扩展GD32
通用Tool Calling、多轮规划、动态Skill或新协议版本。

---

*本文档为UMCA初版开发指导计划v0.1.2，已与架构v0.3.3、协议规范v0.1.2和软件设计v0.1.2同步。后续根据Core实现、Goldfish验证、GD32资源测量和真实硬件互操作结果持续修订。*
