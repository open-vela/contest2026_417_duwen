# UMCA 初版软件详细设计

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档名称 | UMCA 初版软件详细设计 |
| 文档版本 | v0.1.2 |
| 日期 | 2026-08-22 |
| 架构基线 | `UMCA_Architecture_Design_v0.3.3.md` |
| 协议基线 | `UMCA_Protocol_Specification_v0.1.2.md` |
| 开发计划 | `UMCA_Development_Plan_v0.1.2.md` |
| 目标语言 | C11子集，兼容常见嵌入式C编译器 |
| 首发验证环境 | Linux主机单元测试、openvela Goldfish ARM64 |
| 目标硬件 | GD32F470V-START（MCU：GD32F470VKT6） |
| 状态 | 正式开发软件设计冻结候选基线 |

### v0.1.2修订摘要

- 与协议v0.1.2统一PHY的`ops + driver`多实例接口。
- 与协议v0.1.2统一PAL的`ops + user + mutex`注入模型。
- Context和远端节点表增加Boot ID状态，TEARDOWN同步校验会话。
- `umca_init()`增加调用者提供的非零Boot ID。
- Discovery采用“完整校验后原子提交会话与Sequence状态”的处理方式。
- MVP严格拒绝非零Flags、非零QoS和未实现消息类型。
- Topic字符集冻结为ASCII子集，公开API和配置宏完成统一。
- 明确PHY返回值、OFFLINE表项复用、停止语义和固定符合性向量。

---

## 1. 文档定位

本文档把UMCA架构、初版协议和开发计划细化为可直接编码的软件设计，供Linux主机上的开发Agent执行。

本文档重点定义：

- 目录、文件和模块职责。
- 模块依赖方向。
- 公共API和内部接口。
- 静态数据结构和资源所有权。
- Context、节点和定时器状态机。
- 帧发送、接收、发现和Topic路由流程。
- 编译期Profile与源文件裁剪。
- POSIX、Loopback和openvela适配方式。
- Goldfish三节点Demo和ai_agent接入边界。
- 单元测试、集成测试和资源验收。
- Linux Agent的实施顺序和阶段门禁。

本文档不重新定义线级协议。出现冲突时遵循以下优先级：

1. `UMCA_Protocol_Specification_v0.1.2.md`中的线级格式和字段语义。
2. 本文档中的软件模块和API设计。
3. `UMCA_Development_Plan_v0.1.2.md`中的阶段与资源目标。
4. `UMCA_Architecture_Design_v0.3.3.md`中的长期架构愿景。

任何需要修改线级格式的实现问题必须停止编码，先修订协议文档和测试向量。

---

## 2. 设计目标

### 2.1 功能目标

初版实现必须支持：

- 36字节固定帧头、0～256字节Payload和4字节CRC32C。
- 网络字节序逐字段编解码。
- QoS-0 DATA发布订阅。
- Topic规范化及FNV-1a 32位Topic ID。
- 按`<Source DevID, Topic ID>`维护Sequence。
- ANNOUNCE、HEARTBEAT和TEARDOWN。
- 固定容量节点表和本地Topic表。
- 多个独立UMCA Context。
- POSIX PAL、openvela PAL和Loopback PHY。
- Goldfish内Sensor、Agent、Actuator三个逻辑节点。
- ai_agent的`sensor_query`和`device_control`适配。

### 2.2 质量目标

- Core不包含任何平台、驱动或ai_agent头文件。
- 默认不使用动态内存。
- Core不创建线程、队列、信号量或定时器。
- 所有处理函数执行时间有界。
- 非法帧在进入业务回调前被拒绝。
- 未启用模块不参与编译链接。
- Minimal Profile可在资源受限裸机节点上使用。
- 相同协议测试向量在不同平台产生相同结果。

### 2.3 非目标

初版不设计或实现：

- RPC、自动重传和可靠传输。
- 分段重组。
- 网关和多路径。
- DebugService、ACL和加密。
- 动态Topic创建。
- 多回调订阅链。
- Core内部工作线程。
- 通用序列化框架或代码生成器。

---

## 3. 基础接口与会话设计基线

### 3.1 PHY采用实例上下文

协议v0.1.2将PHY定义为`ops + driver`。实现必须使用以下接口，不得退回依赖可变全局状态的单实例形式：

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

同一套`ops`可以服务多个不同`driver`实例，因此Loopback、UART和测试Mock均可自然支持多实例。

返回值契约：

- `init/send/poll`成功返回`UMCA_OK`，平台私有错误必须映射为统一错误码。
- `poll`进入时先将`*received=0`；无帧时返回`UMCA_OK`，有帧时只返回一帧完整数据。
- `get_mtu()`返回0表示未初始化或不可用，正常值不得小于40。
- `get_capabilities()`未定义能力时返回0，未定义位必须为0。

### 3.2 PAL采用平台上下文和宿主mutex

协议v0.1.2规定PAL采用上下文描述符：

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

MVP不由Core创建或销毁mutex。需要线程安全时，mutex由宿主初始化并通过`umca_platform_t`注入。`UMCA_USE_DYNAMIC_MEMORY=0`时`alloc/free`为NULL且Core不得调用。

### 3.3 发布API必须保留Destination

统一采用协议文档中的语义：

```c
int umca_publish(umca_context_t *ctx,
                 uint32_t topic_id,
                 uint64_t destination,
                 const uint8_t *payload,
                 uint16_t payload_length);
```

不得使用缺少Destination参数的简化版本，否则无法表达单播FanCommand。

### 3.4 Boot ID会话语义

协议v0.1.2在ANNOUNCE、HEARTBEAT和TEARDOWN中使用非零32位`boot_id`：

- 同一节点的一次运行期间保持不变。
- 每次新UMCA会话必须更换。
- 同一Source DevID的合法ANNOUNCE出现不同`boot_id`时，接收方建立新会话并清除该Source的全部Sequence历史。
- HEARTBEAT和TEARDOWN只在`boot_id`与当前会话一致时生效，不得创建或切换会话。
- 节点启动后必须先发送ANNOUNCE，再发布DATA。
- `boot_id`由应用或平台生成并注入Core，不由Core使用动态熵源生成。
- `boot_id`仅用于会话区分，不具备认证或安全含义。

这样节点快速重启后可以立即建立新会话，不需要等待15秒OFFLINE超时。

---

## 4. 软件分层与依赖

```text
Demo / ai_agent Tools
          │
          ▼
openvela_umca_agent adapter
          │
          ▼
UMCA public API
          │
 ┌────────┴──────────────────────────────┐
 │ Context / PubSub / Discovery / Stats │
 └────────┬──────────────────────────────┘
          │
 ┌────────┴─────────┐
 ▼                  ▼
PAL descriptor    PHY descriptor
 │                  │
 ▼                  ▼
POSIX/openvela    Loopback/UART
```

允许的依赖：

```text
base ← core ← pubsub
base ← core ← discovery
base ← core ← diagnostics
core → PAL interface
core → PHY interface
ports → public interfaces
phy adapters → public interfaces
application → public interfaces
```

禁止的依赖：

```text
base → discovery
base → openvela
core → ai_agent
core → Demo message
PAL → application
Loopback PHY → discovery
```

---

## 5. 目录与文件设计

```text
umca/
├── include/umca/
│   ├── umca.h
│   ├── umca_config.h
│   ├── umca_protocol.h
│   ├── umca_types.h
│   ├── umca_error.h
│   ├── umca_platform.h
│   ├── umca_phy.h
│   ├── umca_stats.h
│   └── umca_discovery.h
├── src/
│   ├── base/
│   │   ├── umca_byteorder.c
│   │   ├── umca_crc32c.c
│   │   ├── umca_frame.c
│   │   └── umca_topic_id.c
│   ├── core/
│   │   ├── umca_context.c
│   │   ├── umca_process.c
│   │   └── umca_internal.h
│   ├── pubsub/
│   │   ├── umca_topic_table.c
│   │   ├── umca_publish.c
│   │   └── umca_dispatch.c
│   ├── sequence/
│   │   └── umca_sequence.c
│   ├── discovery/
│   │   ├── umca_discovery_codec.c
│   │   ├── umca_discovery_service.c
│   │   └── umca_node_table.c
│   └── diagnostics/
│       └── umca_stats.c
├── ports/
│   ├── posix/
│   │   ├── umca_posix_pal.c
│   │   └── umca_posix_pal.h
│   └── openvela/
│       ├── umca_openvela_pal.c
│       └── umca_openvela_pal.h
├── phy/
│   └── loopback/
│       ├── umca_loopback.c
│       └── umca_loopback.h
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── vectors/
│   ├── mocks/
│   └── CMakeLists.txt
├── examples/
│   └── host_three_nodes/
├── cmake/
├── CMakeLists.txt
├── Kconfig
└── README.md
```

### 5.1 文件职责

| 文件 | 单一职责 |
|------|----------|
| `umca_byteorder.c` | 无对齐要求的大端整数读写 |
| `umca_crc32c.c` | CRC32C计算，不感知帧语义 |
| `umca_frame.c` | 帧头、Payload和CRC的编码解码 |
| `umca_topic_id.c` | Topic校验、规范化检查和FNV-1a |
| `umca_context.c` | Context初始化、启动、停止和状态 |
| `umca_process.c` | 有界poll、接收流水线和定时任务入口 |
| `umca_topic_table.c` | 本地Topic注册、方向和回调 |
| `umca_publish.c` | DATA帧构造和发送 |
| `umca_dispatch.c` | 已验证DATA帧的本地匹配与回调 |
| `umca_sequence.c` | 32位序列比较、TX递增和RX状态 |
| `umca_discovery_codec.c` | 三种发现Payload逐字段编解码 |
| `umca_discovery_service.c` | 周期发送和接收状态转换 |
| `umca_node_table.c` | 远端节点及其Topic的固定容量存储 |
| `umca_stats.c` | 可裁剪统计计数，不输出日志 |

所有文件保持单一职责。公共API不得直接暴露文件内部辅助函数。

---

## 6. 配置系统设计

### 6.1 配置来源

主机CMake构建通过编译定义或生成配置头选择Profile。openvela构建通过Kconfig映射到相同宏。

禁止维护两套含义不同的配置名。

### 6.2 Profile默认值

```c
#if defined(UMCA_PROFILE_MINIMAL)
#  define UMCA_ENABLE_PUBSUB             1
#  define UMCA_ENABLE_DISCOVERY          0
#  define UMCA_ENABLE_RX_SEQUENCE_TRACKING  0
#  define UMCA_ENABLE_REMOTE_TOPIC_NAMES    0
#  define UMCA_ENABLE_STATS              0
#  define UMCA_ENABLE_LOG                0
#  define UMCA_ENABLE_ASSERT             0
#  define UMCA_ENABLE_THREAD_SAFE        0
#elif defined(UMCA_PROFILE_DISCOVERY)
#  define UMCA_ENABLE_PUBSUB             1
#  define UMCA_ENABLE_DISCOVERY          1
#  define UMCA_ENABLE_RX_SEQUENCE_TRACKING  1
#  define UMCA_ENABLE_REMOTE_TOPIC_NAMES    1
#  define UMCA_ENABLE_STATS              1
#  define UMCA_ENABLE_LOG                0
#  define UMCA_ENABLE_ASSERT             1
#  define UMCA_ENABLE_THREAD_SAFE        0
#elif defined(UMCA_PROFILE_CONTEST)
#  define UMCA_ENABLE_PUBSUB             1
#  define UMCA_ENABLE_DISCOVERY          1
#  define UMCA_ENABLE_RX_SEQUENCE_TRACKING  1
#  define UMCA_ENABLE_REMOTE_TOPIC_NAMES    1
#  define UMCA_ENABLE_STATS              1
#  define UMCA_ENABLE_LOG                0
#  define UMCA_ENABLE_ASSERT             1
#  define UMCA_ENABLE_THREAD_SAFE        1
#endif
```

用户显式配置可以覆盖Profile默认值，但经过测试的标准构建必须使用固定Profile组合。

`UMCA_PROFILE_MINIMAL`是静态拓扑的数据面裁剪Profile，不满足比赛MVP的零配置发现和Boot ID热重启恢复要求。需要动态发现或无协调重启的节点必须使用Discovery或Contest Profile。

- `UMCA_ENABLE_RX_SEQUENCE_TRACKING`只控制接收端去重和丢包统计；所有Profile发送DATA时都必须生成线级Sequence。
- `UMCA_ENABLE_REMOTE_TOPIC_NAMES`控制远端发现表是否缓存Topic名称；本地注册仍保留静态名称指针用于哈希和碰撞检查。
- 标准MVP Profile固定发送和接收`Flags=0`、`QoS=0`，不存在对应的降级开关。

### 6.3 容量宏

协议名称保持一致：

```c
#define UMCA_MAX_PAYLOAD          256u
#define UMCA_MAX_FRAME            296u
#define UMCA_MAX_NODES            8u
#define UMCA_MAX_TOPICS           16u
#define UMCA_MAX_TOPICS_PER_NODE  8u
#define UMCA_MAX_RX_SEQUENCE_TRACKS 16u
#define UMCA_RX_QUEUE_DEPTH       4u
#define UMCA_TX_QUEUE_DEPTH       4u
#define UMCA_MAX_TOPIC_NAME       63u
#define UMCA_POLL_RX_BUDGET       4u
```

`UMCA_RX_QUEUE_DEPTH`和`UMCA_TX_QUEUE_DEPTH`是PHY Adapter容量建议，不要求Core重复创建同名队列。

### 6.4 编译期检查

`umca_config.h`必须使用`#error`或静态断言检查：

- 恰好选择一个Profile。
- `UMCA_MAX_PAYLOAD <= 256`。
- `UMCA_MAX_FRAME >= 40 + UMCA_MAX_PAYLOAD`。
- Discovery启用时`UMCA_MAX_NODES > 0`。
- Discovery启用时`UMCA_MAX_TOPICS_PER_NODE > 0`。
- MVP Discovery启用时必须同时启用`UMCA_ENABLE_REMOTE_TOPIC_NAMES`，否则无法执行协议要求的Topic碰撞检测。
- 远端Topic名称缓存启用时`UMCA_MAX_TOPIC_NAME`范围为1～63。
- `UMCA_MAX_TOPICS`不为0。
- RX Sequence跟踪启用时`UMCA_MAX_RX_SEQUENCE_TRACKS > 0`。
- 未实现功能宏不得为1。
- 统计关闭时不得引用统计对象。

### 6.5 源文件级裁剪

CMake示意：

```text
base和core：始终编译
pubsub：UMCA_ENABLE_PUBSUB=1时编译
sequence：UMCA_ENABLE_RX_SEQUENCE_TRACKING=1时编译
discovery：UMCA_ENABLE_DISCOVERY=1时编译
diagnostics：UMCA_ENABLE_STATS=1时编译
```

不得只依赖函数内部`#if`实现整模块裁剪。

---

## 7. 公共类型设计

### 7.1 基础标识

```c
typedef uint64_t umca_devid_t;
typedef uint32_t umca_topic_id_t;
typedef uint32_t umca_sequence_t;
```

### 7.2 Context状态

```c
typedef enum {
    UMCA_CONTEXT_UNINITIALIZED = 0,
    UMCA_CONTEXT_INITIALIZED,
    UMCA_CONTEXT_RUNNING,
    UMCA_CONTEXT_ID_CONFLICT,
    UMCA_CONTEXT_STOPPED
} umca_context_state_t;
```

### 7.3 Topic方向

```c
enum {
    UMCA_TOPIC_PUBLISHER  = 1u << 0,
    UMCA_TOPIC_SUBSCRIBER = 1u << 1
};
```

### 7.4 接收消息视图

```c
typedef struct {
    uint16_t flags;
    uint8_t message_type;
    uint8_t qos;
    uint8_t ttl;
    umca_devid_t destination;
    umca_devid_t source;
    umca_topic_id_t topic_id;
    umca_sequence_t sequence;
    const uint8_t *payload;
    uint16_t payload_length;
} umca_message_t;
```

`payload`指向Context的RX缓冲区，只在当前回调期间有效。

### 7.5 Topic回调

```c
typedef void (*umca_topic_callback_t)(umca_context_t *ctx,
                                      const umca_message_t *message,
                                      void *user_data);
```

回调允许调用`umca_publish()`，但不得调用：

- `umca_deinit()`。
- `umca_start()`或`umca_stop()`。
- 修改当前Context的Topic表。

需要修改订阅关系时，应在回调返回后由应用任务执行。

---

## 8. 内部数据结构

### 8.1 线级帧主机表示

```c
typedef struct {
    uint16_t flags;
    uint8_t message_type;
    uint8_t qos;
    uint8_t ttl;
    umca_devid_t destination;
    umca_devid_t source;
    umca_topic_id_t topic_id;
    umca_sequence_t sequence;
    uint16_t payload_length;
} umca_frame_header_t;
```

该结构只用于主机表示，禁止直接复制到线级缓冲区。

### 8.2 本地Topic表项

```c
typedef struct {
    bool used;
    umca_topic_id_t topic_id;
    const char *name;
    uint8_t name_length;
    uint8_t direction;
    umca_sequence_t next_tx_sequence;
    umca_topic_callback_t callback;
    void *callback_user;
} umca_local_topic_t;
```

设计约束：

- `name`由应用提供，生命周期必须覆盖Context。
- 推荐使用静态常量字符串，不复制名称以节约RAM。
- 一个Context中的一个Topic最多一个回调。
- 同名重复注册合并方向；不同名称产生相同ID时返回碰撞错误。
- 发布方向未注册时拒绝`umca_publish()`。

### 8.3 RX Sequence状态

```c
typedef struct {
    bool initialized;
    umca_sequence_t last;
} umca_rx_sequence_state_t;
```

### 8.4 远端Topic表项

```c
typedef struct {
    bool used;
    umca_topic_id_t topic_id;
    uint8_t direction;
#if UMCA_ENABLE_REMOTE_TOPIC_NAMES
    uint8_t name_length;
    char name[UMCA_MAX_TOPIC_NAME + 1u];
#endif
} umca_remote_topic_t;
```

字符串内部补`\0`只用于本地诊断，不属于线级Payload。

### 8.5 远端节点表项

```c
typedef struct {
    bool used;
    umca_devid_t dev_id;
    uint32_t boot_id;
    umca_node_state_t state;
    uint32_t capabilities;
    uint32_t last_seen_ms;
    uint32_t state_changed_ms;
    uint32_t last_uptime_ms;
    uint8_t remote_status;
    uint8_t topic_count;
    umca_remote_topic_t topics[UMCA_MAX_TOPICS_PER_NODE];
} umca_remote_node_t;
```

节点表采用固定数组和线性查找。MVP最大8个节点，线性查找比哈希表更简单且资源可预测。

### 8.6 RX Sequence跟踪表项

Sequence不能依附于Discovery节点Topic表，因为DATA可能早于ANNOUNCE到达，Minimal Profile也可能关闭Discovery。

```c
typedef struct {
    bool used;
    umca_devid_t source;
    umca_topic_id_t topic_id;
    umca_rx_sequence_state_t state;
} umca_rx_sequence_entry_t;
```

- 固定数组按`<source, topic_id>`线性查找。
- 系统消息使用`topic_id=0`，无需单独存储结构。
- 默认容量`UMCA_MAX_RX_SEQUENCE_TRACKS=16`，比赛实际键数量应远小于该值。
- 表满时拒绝新的Sequence键并返回或统计`UMCA_ERR_CAPACITY`，不得覆盖仍在线节点的历史。
- 节点进入OFFLINE或收到TEARDOWN时，清除该Source的全部Sequence表项。

### 8.7 统计对象

```c
typedef struct {
    uint32_t tx_frames;
    uint32_t tx_errors;
    uint32_t rx_frames;
    uint32_t rx_delivered;
    uint32_t rx_crc_errors;
    uint32_t rx_length_errors;
    uint32_t rx_version_errors;
    uint32_t rx_semantic_errors;
    uint32_t rx_duplicates;
    uint32_t rx_stale;
    uint32_t rx_estimated_lost;
    uint32_t rx_unsubscribed;
    uint32_t node_online_events;
    uint32_t node_offline_events;
    uint32_t capacity_errors;
} umca_stats_t;
```

计数溢出允许无符号自然回绕。Stats关闭时整个结构和更新代码不得存在。

### 8.8 Context

MVP采用公共头文件可确定大小的调用者持有结构，避免不透明对象带来的动态分配或复杂存储对齐。

```c
struct umca_context {
    umca_context_state_t state;
    umca_devid_t local_dev_id;
    uint32_t local_boot_id;
    umca_platform_t platform;
    umca_phy_t phy;
    uint32_t last_heartbeat_ms;
    uint32_t last_announce_ms;
    umca_sequence_t system_tx_sequence;
    uint8_t dispatch_depth;
    umca_local_topic_t topics[UMCA_MAX_TOPICS];
#if UMCA_ENABLE_DISCOVERY
    umca_remote_node_t nodes[UMCA_MAX_NODES];
#endif
#if UMCA_ENABLE_RX_SEQUENCE_TRACKING
    umca_rx_sequence_entry_t rx_sequences[UMCA_MAX_RX_SEQUENCE_TRACKS];
#endif
#if UMCA_ENABLE_STATS
    umca_stats_t stats;
#endif
    uint8_t rx_frame[UMCA_MAX_FRAME];
    uint8_t tx_frame[UMCA_MAX_FRAME];
};
```

Context内部字段不承诺ABI稳定。应用只能通过公开API访问，不得直接修改字段。

为防止Context尺寸失控，测试必须输出`sizeof(umca_context_t)`并纳入资源报告。

---

## 9. 公共API设计

### 9.1 初始化与生命周期

```c
int umca_init(umca_context_t *ctx,
              umca_devid_t local_dev_id,
              uint32_t local_boot_id,
              const umca_platform_t *platform,
              const umca_phy_t *phy);

int umca_start(umca_context_t *ctx);
int umca_stop(umca_context_t *ctx, uint8_t teardown_reason);
void umca_deinit(umca_context_t *ctx);
```

语义：

- `init`校验DevID和非零Boot ID，清零内部状态并初始化PHY，但不发送ANNOUNCE。
- `init`必须校验PAL/PHY描述符及所有当前Profile必需的函数指针；Thread Safe Profile还必须校验非NULL mutex和lock/unlock。
- PHY初始化成功后`get_mtu()`必须至少为40，否则`init`失败并回滚已初始化资源。
- 应用在`init`后注册全部Topic。
- Discovery启用时，`start`验证ANNOUNCE长度并立即发送ANNOUNCE；Discovery关闭时直接进入RUNNING。
- Discovery启用时，`stop`尝试发送携带当前Boot ID的TEARDOWN；无论发送是否成功都进入STOPPED，发送失败时返回`UMCA_ERR_PHY`。Discovery关闭时直接进入STOPPED。
- `deinit`释放PHY平台资源，但不释放Context存储。
- 重复`start`、非法状态`stop`返回明确错误；对UNINITIALIZED或已deinit对象再次`deinit`必须安全无副作用。

### 9.2 Topic注册

```c
int umca_topic_register(umca_context_t *ctx,
                        const char *name,
                        uint8_t direction,
                        umca_topic_callback_t callback,
                        void *user_data,
                        umca_topic_id_t *topic_id_out);
```

规则：

- 必须在`start`前调用。
- Publisher可不提供callback。
- Subscriber必须提供callback。
- `direction`至少包含一个有效位，Publisher/Subscriber之外的位必须为0。
- 同一Topic同时发布和订阅时使用方向位组合。
- 名称必须已经规范化；MVP不自动修正非法名称。
- `topic_id_out`可为NULL。
- 同名重复注册时可以合并方向；若重复Subscriber提供不同callback或user_data，则返回`UMCA_ERR_INVALID_ARG`，不得静默替换。

初版不提供运行期注销和动态修改，以保持ANNOUNCE内容稳定。

### 9.3 发布

```c
int umca_publish(umca_context_t *ctx,
                 umca_topic_id_t topic_id,
                 umca_devid_t destination,
                 const uint8_t *payload,
                 uint16_t payload_length);
```

约束：

- Context必须处于RUNNING。
- ID冲突状态禁止发布DATA。
- Topic必须注册Publisher方向。
- `payload_length > 0`时Payload不得为NULL。
- Payload不得超过编译配置和PHY MTU。
- 广播使用`UMCA_BROADCAST_DEVID`。

### 9.4 有界处理

```c
int umca_poll(umca_context_t *ctx, uint16_t *processed_frames);
```

单次调用最多处理`UMCA_POLL_RX_BUDGET`帧，并执行一次定时器检查。

- 无数据不是错误，返回`UMCA_OK`且processed为0。
- `processed_frames`可以为NULL；非NULL时统计本次从PHY取出的完整帧数量，包括随后被协议校验拒绝的帧。
- 单个非法帧只更新统计，不中止后续预算内帧处理。
- PHY本身失败时返回`UMCA_ERR_PHY`。
- 不无限循环等待数据。

### 9.5 查询接口

```c
umca_context_state_t umca_get_state(const umca_context_t *ctx);

#if UMCA_ENABLE_DISCOVERY
int umca_node_get(const umca_context_t *ctx,
                  umca_devid_t dev_id,
                  umca_node_info_t *out);
size_t umca_node_count(const umca_context_t *ctx,
                       umca_node_state_t minimum_state);
#endif

#if UMCA_ENABLE_STATS
int umca_stats_get(const umca_context_t *ctx, umca_stats_t *out);
void umca_stats_reset(umca_context_t *ctx);
#endif
```

查询接口复制快照，不返回内部表项指针。

---

## 10. 字节序、CRC与帧Codec

### 10.1 字节序辅助函数

提供不依赖对齐的内部函数：

```c
uint16_t umca_read_be16(const uint8_t *p);
uint32_t umca_read_be32(const uint8_t *p);
uint64_t umca_read_be64(const uint8_t *p);
void umca_write_be16(uint8_t *p, uint16_t value);
void umca_write_be32(uint8_t *p, uint32_t value);
void umca_write_be64(uint8_t *p, uint64_t value);
```

不得通过未对齐指针强制转换读写整数。

### 10.2 CRC32C接口

```c
uint32_t umca_crc32c(const uint8_t *data, size_t length);
```

初版优先实现256项查表版本，并允许后续通过配置提供小表或逐位版本。不同实现必须通过相同固定向量。

CRC覆盖：

```text
frame[2] 至 frame[36 + payload_length - 1]
```

覆盖长度为：

```text
34 + payload_length
```

### 10.3 编码接口

```c
int umca_frame_encode(const umca_frame_header_t *header,
                      const uint8_t *payload,
                      uint8_t *output,
                      size_t output_capacity,
                      size_t *output_length);
```

编码顺序：

1. 校验参数。
2. 校验Header语义和Payload长度；MVP要求`flags=0`、`qos=0`、`ttl>0`、Source和Destination均非0。
3. 计算完整长度。
4. 检查输出容量。
5. 逐字段写入固定偏移。
6. 复制Payload。
7. 计算CRC32C。
8. 以大端写入CRC。
9. 最后设置`output_length`。

失败时不得返回部分帧作为有效输出。

### 10.4 解码接口

```c
int umca_frame_decode(const uint8_t *frame,
                      size_t frame_length,
                      umca_frame_header_t *header,
                      const uint8_t **payload);
```

校验顺序严格遵守协议规范：

1. 最小长度40字节。
2. Magic。
3. Version。
4. Header Length。
5. Flags必须为0，否则返回`UMCA_ERR_UNSUPPORTED_FLAGS`。
6. QoS必须为0，否则返回`UMCA_ERR_UNSUPPORTED_QOS`。
7. Reserved必须为0，Payload Length不得超限。
8. 实际长度完全匹配。
9. CRC32C。
10. Source和Destination DevID、TTL、消息类型和Topic语义。

REQUEST、RESPONSE、ERROR和保留消息类型均返回`UMCA_ERR_UNSUPPORTED_TYPE`。Discovery消息必须使用Topic 0和广播Destination，DATA必须使用非零业务Topic。

`payload`只指向输入帧内部，不复制。

---

## 11. Topic设计

### 11.1 Topic校验

```c
int umca_topic_validate(const char *name, size_t length);
int umca_topic_id_from_name(const char *name,
                            size_t length,
                            umca_topic_id_t *out);
```

校验：

- 长度1～63字节。
- 首字符为`/`。
- 根Topic外末尾不得为`/`。
- 不得出现`//`。
- 路径段不得为`.`或`..`。
- 路径段只允许ASCII字母、数字、下划线、连字符和点号。
- 任何非ASCII字节必须拒绝。
- MVP不执行Unicode规范化；扩展到完整UTF-8必须升级协议规范并增加跨平台向量。

### 11.2 FNV-1a

```text
hash = 0x811C9DC5
for each byte:
    hash = hash XOR byte
    hash = hash * 0x01000193  // u32自然回绕
```

结果为0时返回错误，要求应用更换名称。

### 11.3 碰撞处理

- 注册本地Topic时比较ID和完整名称。
- 解析ANNOUNCE时比较远端Topic和已知名称。
- 同ID不同名称标记碰撞，拒绝相应Topic进入远端表。
- 碰撞不应破坏同一ANNOUNCE中的其他合法Topic。

---

## 12. Sequence设计

### 12.1 TX Sequence

- 每个本地业务Topic维护独立`next_tx_sequence`。
- 系统消息Topic 0使用Context的`system_tx_sequence`。
- 初始化时两类`next`值均设为1；0只会在32位自然回绕后出现。
- 发送成功或失败后Sequence是否递增必须固定。

MVP规定：在帧成功交给PHY后递增。PHY拒绝发送时保留当前Sequence，便于应用重试仍表示同一次待发送数据。

### 12.2 RX比较

```c
typedef enum {
    UMCA_SEQUENCE_FIRST,
    UMCA_SEQUENCE_NEW,
    UMCA_SEQUENCE_DUPLICATE,
    UMCA_SEQUENCE_STALE
} umca_sequence_result_t;
```

比较算法使用无符号差值，避免将超出范围的`uint32_t`直接转换为有符号整数：

```text
delta = (uint32_t)(incoming - last)
delta == 0                  → DUPLICATE
0 < delta < 0x80000000     → NEW
delta >= 0x80000000        → STALE
```

第一次收到直接返回FIRST。正向delta大于1时，估算丢包数增加`delta - 1`。

差值恰好跨越半个32位空间时无法确定先后，按STALE处理并记录。

### 12.3 Boot ID变化与Sequence重置

对于ANNOUNCE：

1. 先完成帧基础校验和ANNOUNCE Payload完整校验。
2. 读取非零`boot_id`。
3. 若节点不存在，建立新会话并把当前系统Sequence视为FIRST。
4. 若`boot_id`与已记录值相同，执行正常Sequence检查。
5. 若`boot_id`不同，清除该Source的全部Sequence表项，把当前ANNOUNCE视为新会话第一帧。
6. 只有ANNOUNCE处理成功后，才提交新的`boot_id`和Sequence状态。

不得在发现Payload尚未完全验证时清除旧会话状态，防止畸形帧破坏节点表。

HEARTBEAT和TEARDOWN不创建或切换会话。未知DevID或`boot_id`不匹配时直接忽略且不提交Sequence；只有匹配当前会话时才执行正常Sequence检查和状态处理。

### 12.4 状态提交时机

Sequence历史只在帧通过所有基础校验并且语义允许处理后更新。CRC错误、长度错误、非法消息类型或非法`boot_id`不得污染Sequence状态。

---

## 13. Pub/Sub设计

### 13.1 发布流程

```text
umca_publish
  ↓
检查Context状态
  ↓
查找本地Topic并确认Publisher方向
  ↓
检查Destination、Payload和PHY MTU
  ↓
读取当前Topic Sequence
  ↓
编码到ctx->tx_frame
  ↓
phy.send复制或接管到自身静态队列
  ↓
发送成功后递增Sequence和统计
```

Core不维护额外TX队列。异步UART或WiFi的队列属于PHY Adapter，从而避免重复缓存完整帧。

### 13.2 接收路由

DATA帧通过全部校验后：

1. Destination既不是本节点也不是广播：忽略。
2. Source等于本节点且未启用测试回环：忽略。
3. Topic未订阅：增加`rx_unsubscribed`，不报错。
4. Topic已订阅：构造`umca_message_t`并调用回调。

### 13.3 锁与回调

线程安全Profile中：

1. 锁内完成表查找、Sequence和统计更新。
2. 复制回调函数指针及user_data到局部变量。
3. 释放锁。
4. 调用应用回调。

不得持锁调用业务回调。

### 13.4 回调发布

RX和TX使用不同缓冲区，因此回调中允许同步调用`umca_publish()`。Core通过`dispatch_depth`检测不允许的生命周期修改，不需要递归锁。

---

## 14. 服务发现设计

### 14.1 Context启动

```text
INITIALIZED
  ↓ umca_start
验证配置；Discovery启用时验证Topic数量和ANNOUNCE编码长度
  ↓
PHY已初始化
  ↓
Discovery启用时发送ANNOUNCE
  ↓
RUNNING
```

如果ANNOUNCE无法编码或发送，`start`返回错误且保持INITIALIZED，允许修正后重试。

### 14.2 定时发送

在`umca_poll()`中使用无符号差值：

```c
elapsed = (uint32_t)(now - last_time);
```

- `elapsed >= 5000`发送HEARTBEAT。
- `elapsed >= 30000`发送ANNOUNCE。
- 不使用`now >= deadline`形式，避免计数器回绕问题。

若发送失败，记录错误；下一次poll不应忙循环重试。更新时间策略采用“尝试时更新时间”，防止故障链路持续占满CPU。

### 14.3 ANNOUNCE编码

启动前预计算Payload长度：

```text
9 + Σ(6 + topic_name_length)
```

- 最大8个Topic。
- 总长度不得超过256。
- 固定前缀为`capabilities:u32 + boot_id:u32 + topic_count:u8`。
- `boot_id`必须等于Context的`local_boot_id`且非零。
- 不静默截断Topic列表。
- 超限时`umca_start()`返回`UMCA_ERR_PAYLOAD_TOO_LARGE`。

### 14.4 ANNOUNCE解析

采用两遍解析，避免半更新节点表：

第一遍：

- 检查最小长度。
- 检查capabilities保留位。
- 检查`boot_id`非零。
- 检查topic_count。
- 遍历并检查每个Entry边界、方向、名称和Topic ID。
- 检查Payload恰好消费完毕。
- 检查Topic碰撞。

第二遍：

- 获取或创建节点表项。
- 清理旧远端Topic列表。
- 写入全部合法Topic。
- 写入或更新远端`boot_id`。
- 如`boot_id`发生变化，清除该Source的全部旧Sequence历史。
- 更新capabilities、last_seen和ONLINE状态。

不得在第一遍失败时留下部分节点信息。

### 14.5 节点状态机

```text
空表项 --ANNOUNCE/HEARTBEAT(boot_id非零)--> ONLINE
ONLINE --达到10秒未更新-------> STALE
STALE  --收到合法消息---------> ONLINE
ONLINE/STALE --达到15秒-------> OFFLINE
ONLINE/STALE --匹配boot_id的TEARDOWN--> OFFLINE
OFFLINE --ANNOUNCE-------------> ONLINE并建立新会话
ONLINE/STALE --ANNOUNCE携带新boot_id--> ONLINE并重置该Source的Sequence历史
```

未知DevID的HEARTBEAT必须忽略，不创建节点表项；节点启动时先发ANNOUNCE，接收方只对已知DevID处理HEARTBEAT。HEARTBEAT固定Payload为`boot_id:u32 + uptime_ms:u32 + status:u8`，共9字节。

TEARDOWN固定Payload为`boot_id:u32 + reason:u8`，共5字节：

- `boot_id`与当前节点会话一致时才进入OFFLINE。
- 未知节点、Boot ID为0或不匹配时忽略，不修改节点状态和Sequence表。
- 进入OFFLINE时记录`state_changed_ms`并清除该Source的Sequence历史。

节点表分配顺序固定为：

1. 已存在相同DevID的表项。
2. 第一个空表项。
3. 最早进入OFFLINE的表项。
4. 无可复用表项时返回`UMCA_ERR_CAPACITY`。

“最早”通过当前时间与`state_changed_ms`的无符号差值选择最大elapsed，正确处理毫秒回绕。不得驱逐ONLINE或STALE节点。

### 14.6 DevID冲突

收到Source DevID等于本节点的ANNOUNCE且不是显式测试回环时：

- Context进入`UMCA_CONTEXT_ID_CONFLICT`。
- 禁止发送任何UMCA帧并停止发现定时发送。
- 保留poll和诊断查询能力。
- 增加语义错误统计。
- 不自动生成新的DevID。

由应用修正配置并重新初始化Context。ID_CONFLICT状态调用`stop`时不得发送TEARDOWN，只进入STOPPED。

---

## 15. 接收处理流水线

```text
umca_poll
  ↓
phy.poll(ctx->rx_frame)
  ↓
最小长度
  ↓
frame_decode：Magic/Version/Header/Length/CRC
  ↓
Destination/Source/TTL/Type/Topic语义
  ↓
发现消息完整解析boot_id；必要时准备新会话
  ↓
查找或准备Sequence状态，执行重复/旧帧过滤
  ↓
Discovery完整处理并原子提交会话状态，或提交DATA Sequence后分发
  ↓
更新统计
  ↓
下一帧，直到预算耗尽
  ↓
定时器与节点超时检查
```

### 15.1 本地错误处理边界

MVP不发送ERROR帧。任何接收错误均执行：

1. 返回对应本地错误码或更新统计。
2. 丢弃当前帧。
3. 不调用Topic回调。
4. 不向网络发送错误响应，避免错误风暴。

FanCommand的业务执行失败通过FanState的`result`反馈，不属于UMCA协议错误。

### 15.2 一帧失败不影响下一帧

`umca_poll()`处理队列中的非法帧后继续处理预算内后续帧。只有PHY本身返回不可恢复错误时才结束本次poll并返回`UMCA_ERR_PHY`。

---

## 16. Loopback PHY设计

### 16.1 对象模型

```c
typedef struct {
    bool used;
    uint16_t length;
    uint8_t data[UMCA_MAX_FRAME];
} umca_loopback_slot_t;

typedef struct {
    umca_loopback_slot_t slots[UMCA_RX_QUEUE_DEPTH];
    uint8_t read_index;
    uint8_t write_index;
    uint8_t count;
} umca_loopback_queue_t;

typedef struct umca_loopback_bus umca_loopback_bus_t;

typedef struct {
    umca_loopback_bus_t *bus;
    uint8_t endpoint_index;
    umca_loopback_queue_t rx_queue;
    uint32_t fault_flags;
} umca_loopback_endpoint_t;
```

### 16.2 发送语义

- Endpoint发送时把完整帧复制到其他已连接Endpoint的RX队列。
- 默认不回送发送者自身。
- Core负责Destination过滤，Bus模拟共享物理媒介。
- 发送前先检查所有目标队列容量；任一目标队列已满时不复制到任何目标，并返回容量错误。
- 为保持确定性，发送按Endpoint索引顺序复制。

### 16.3 故障注入

每个Endpoint或Bus支持测试配置：

- 丢弃下一帧。
- 重复下一帧。
- 翻转下一帧指定字节。
- 队列强制满。

故障注入接口只用于测试，不进入UMCA公共API。

### 16.4 并发

主机集成测试默认单线程轮询三个Context。需要多线程测试时，Bus使用宿主提供的锁；Core不感知Bus锁。

---

## 17. POSIX PAL设计

### 17.1 时间

- 使用`CLOCK_MONOTONIC`。
- `time_ms(user)`返回低32位毫秒计数。
- `time_us()`仅在启用时提供真实微秒精度。
- 不使用系统墙钟。

### 17.2 同步

- 单线程测试Profile不启用mutex。
- 多线程Profile由调用者创建`pthread_mutex_t`并注入。
- PAL只执行lock/unlock，不分配mutex。

### 17.3 主机测试能力

测试Mock PAL应支持：

- 手动推进虚拟时间。
- 固定返回时间。
- 模拟32位毫秒回绕。
- 记录断言调用。
- 统计锁次数。

协议单元测试优先使用Mock时间，避免真实sleep导致测试缓慢和不稳定。

---

## 18. openvela PAL与运行模型

### 18.1 PAL映射

Linux Agent必须先检查当前openvela/NuttX配置中可用API，再确定具体函数，不得凭名称猜测。

目标映射：

| UMCA能力 | openvela实现方向 |
|----------|------------------|
| `time_ms` | 单调系统Tick或clock API |
| critical | NuttX临界区API，保存并恢复原状态 |
| mutex | 调用者持有的mutex对象 |
| log | 可选，映射现有日志设施或关闭 |
| assert | 可选，映射现有断言或错误上报 |

### 18.2 Goldfish运行任务

模拟阶段使用一个UMCA Service任务管理三个Context：

```text
while running:
    poll(sensor_ctx)
    poll(agent_ctx)
    poll(actuator_ctx)
    run_virtual_sensor_tick()
    run_agent_adapter_tick()
    sleep_or_wait(short_interval)
```

优点：

- 行为确定。
- 不需要三个额外线程。
- 降低并发问题和栈占用。
- 易于单步诊断。

硬件阶段通常只有一个GD32 Context，仍可复用相同Service入口。

### 18.3 GD32F470VKT6 编译、烧录和调试边界

GD32F470V-START 的 MCU 型号为 GD32F470VKT6。Linux 端使用 NuttX 官方
`configure.sh + make` 路径交叉编译；Windows 端只负责连接板载 GD-Link/CMSIS-DAP
进行烧录、复位、串口观察和 GDB 调试。当前 openvela CMake 流程在 GD32F4 架构目录
缺少 `CMakeLists.txt`，因此不得把 `build.sh --cmake` 失败误判为 UMCA 编译错误。

推荐的 Linux 构建命令：

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

板级 `nsh` 控制台固定为 USART0，PB6 为 TX、PB7 为 RX、AF7、115200 8N1，对应
`/dev/ttyS0`。UMCA 固定使用 UART3，PC10 为 TX、PC11 为 RX、AF8、115200 8N1，
对应 `/dev/ttyS1`。两路串口不复用；UART3 采用 3.3 V TTL、共地、无 RTS/CTS，
首轮联调关闭 DMA。PC10/PC11 与 SDIO_D2/SDIO_D3 复用，因此该构建必须禁用 SDIO。
PA9 与 PD2 因板载 USB 网络负载不再用作 UART。
构建 Profile 同时关闭 SPI5、ENET、EXMC、TLI 和 DCI 等不适用 LQFP100
封装或 V-START 走线的继承配置。默认 200 MHz 下 PLLQ=50 MHz，当前时钟
代码未完成 IRC48M 域切换，因此 USB FS、SDIO 和 TRNG 也在该 Profile 中
关闭。

UMCA UART PHY 使用独立字节流适配器，组帧规则为搜索 UMCA `0x55 0x4D`、读取固定
36 字节帧头、按 Payload 长度收齐完整帧，再交给 Core 校验 CRC；适配器不在 ISR 中
调用 Core。默认 NSH 命令为 `umca_gd32 start <dev-id> <boot-id>`，也允许在诊断时以
四参数形式覆盖设备路径。`umca_gd32 pinout` 用于在实板端核对编译进固件的接口定义。
GD32 的业务角色与对端类型仍由应用部署决定，不改变已冻结的 UART 物理接口。

本阶段不修改 `.vscode/`，也不依赖任何 IDE 任务或用户目录配置。

当前上游 GD32F4 `arm_earlyserialinit()` 对稀疏串口表缺少空指针保护；本地 NuttX
修复为先判断 `g_uart_devs[i]` 再访问私有状态。该修复是硬件启动前置条件，独立于
UMCA Core 和 UART PHY，并应在公共 NuttX 仓单独维护。

### 18.4 NSH命令

应用层提供：

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

NSH命令不得直接访问Context内部字段，应调用公开查询API。

---

## 19. Demo消息Codec

Demo消息属于应用目录，不进入`umca/`：

```text
app/openvela_umca_agent/demo/
├── demo_topics.h
├── demo_codec.c
├── virtual_sensor.c
└── virtual_actuator.c
```

### 19.1 API

```c
int demo_encode_temperature_sample(const demo_temperature_sample_t *msg,
                                   uint8_t output[9]);
int demo_decode_temperature_sample(const uint8_t *payload,
                                   size_t length,
                                   demo_temperature_sample_t *out);

int demo_encode_threshold_event(...);   /* 13 bytes */
int demo_decode_threshold_event(...);
int demo_encode_fan_command(...);       /* 6 bytes */
int demo_decode_fan_command(...);
int demo_encode_fan_state(...);         /* 10 bytes */
int demo_decode_fan_state(...);
```

仍然逐字段编码，不发送C结构体镜像。

### 19.2 Virtual Sensor

状态：

```c
typedef struct {
    int32_t temperature_mc;
    int32_t on_threshold_mc;
    int32_t off_threshold_mc;
    uint32_t sample_period_ms;
    uint32_t last_sample_ms;
    uint32_t last_event_ms;
    bool high_state;
} virtual_sensor_t;
```

行为：

- 默认每1000ms发布TemperatureSample。
- 温度从低区进入高区时发布高阈值事件。
- 温度从高区下降到关闭阈值时发布低阈值事件。
- 相同方向事件至少间隔10秒。
- 测试接口允许设置模拟温度。

### 19.3 Virtual Actuator

收到FanCommand后：

1. 检查Payload长度和字段范围。
2. 应用目标状态。
3. 保留原`request_id`。
4. 发布FanState。

测试模式支持：

- 正常确认。
- 拒绝命令。
- 延迟确认。
- 丢弃确认。

---

## 20. ai_agent适配设计

### 20.1 目录

```text
app/openvela_umca_agent/
├── adapter/
│   ├── umca_agent_adapter.c
│   ├── umca_agent_adapter.h
│   ├── umca_sample_cache.c
│   ├── umca_request_tracker.c
│   └── umca_event_bridge.c
├── tools/
│   ├── sensor_query_tool.c
│   └── device_control_tool.c
└── demo/
```

### 20.2 接入前置调查

Linux Agent在写适配代码前必须先读取现有ai_agent：

- Tool Provider注册接口。
- Message Bus接口。
- Agent主动事件入口。
- 线程和回调上下文。
- JSON输入输出约定。
- 静态或动态内存使用规则。

不得编造不存在的ai_agent API。调查结果应记录到项目文档后再实现适配。

当前 openvela 接入已核对并使用实际接口：`tool_registry_register_provider()` 注册
provider，`tool_registry_invalidate()` 使工具缓存失效，`tool_registry_execute()`
执行 Tool，`tool_registry_get_tools_json()` 查询当前工具清单。UMCA 适配层通过弱
引用这些符号，因此未链接 ai_agent 时三节点服务仍可运行；在 app-level CMake 中
加入现有 `packages/ai_agent` target 后，provider 才在运行时注册。

### 20.3 样本缓存

```c
typedef struct {
    bool present;
    bool valid;
    int32_t temperature_mc;
    uint32_t sample_time_ms;
    uint32_t received_time_ms;
    umca_devid_t source_dev_id;
} umca_temperature_cache_t;
```

UMCA回调只完成：

- 解码。
- 校验来源。
- 更新固定缓存。
- 投递轻量事件。

不得在UMCA回调中调用LLM或等待网络。

### 20.4 `sensor_query`

执行：

1. 检查传感器节点是否ONLINE。
2. 检查缓存是否存在且quality有效。
3. 计算`age_ms = now - received_time_ms`。
4. 超过配置有效期返回过期错误。
5. 返回温度、年龄、来源和在线状态。

不得在查询时生成模拟值填充失败结果。

### 20.5 请求追踪

初版只需要有限并发，采用固定数组：

```c
typedef struct {
    bool used;
    uint32_t request_id;
    uint32_t deadline_ms;
    bool completed;
    uint8_t state;
    uint8_t result;
} umca_pending_request_t;
```

默认容量固定为4个，通过`UMCA_AGENT_MAX_PENDING_REQUESTS`在编译期配置为1～4。

`request_id`生成规则：

- 使用适配层32位递增计数器并允许自然回绕。
- `0`为无效值，回绕到0时跳到1。
- 分配前检查全部活跃槽位，不得复用仍在等待的ID。
- 找不到可用槽位或唯一ID时返回`UMCA_ERR_CAPACITY`。
- `request_id`只在目标设备和当前运行会话内用于匹配，不作为全网永久标识。

### 20.6 `device_control`

执行：

1. 校验设备和命令参数。
2. 检查Actuator节点ONLINE。
3. 分配非零`request_id`。
4. 占用一个固定请求槽位。
5. 发布单播FanCommand。
6. 等待Agent适配层事件或轮询完成标志，最长1000ms。
7. 匹配FanState的来源和`request_id`。
8. 根据`result`返回成功或失败。
9. 释放请求槽位。

等待属于适配层，不进入UMCA Core。

实现细节：请求发布调用 `umca_publish()` 的目的参数指向 Actuator DevID；FanState
匹配同时检查 `message->source`、`request_id` 和 `result`。成功输出包含
`request_id` 与执行后的 `state`，超时/拒绝输出 `timeout_or_rejected`。适配器当前
使用单个固定 pending 请求状态，后续若开放并发控制再按固定容量数组扩展，不能引入
无界动态队列。

### 20.7 主动事件桥

ThresholdEvent回调只把固定结构投递到应用事件队列。Agent线程读取事件后：

- 首选调用ai_agent决策入口。
- LLM不可用或超时时执行本地规则。
- 调用`device_control`。
- 记录执行结果。

### 20.8 本地规则兜底

```text
direction=高于阈值 → fan on
direction=低于阈值 → fan off
```

该路径用于：

- 无API Key。
- 网络断开。
- LLM超时。
- 比赛现场稳定演示。

本地规则不伪装为LLM决策，日志中应标明决策来源。

---

## 21. 锁、线程和中断规则

### 21.1 Core规则

- Core无内部线程。
- Core API默认由一个协议任务调用。
- Thread Safe Profile允许其他任务调用`publish`和查询API。
- 业务回调在调用`umca_poll()`的任务上下文执行。
- 不在ISR中调用`umca_poll()`、`publish()`或业务回调。

### 21.2 ISR到PHY

硬件UART RX ISR只负责：

- 写入驱动环形缓冲。
- 更新轻量状态。
- 唤醒协议任务。

完整帧识别和UMCA解码在任务上下文执行。

### 21.3 锁顺序

如存在多个锁，固定顺序：

```text
应用适配锁 → UMCA Context锁 → PHY锁
```

实际MVP应尽量避免同时持有多个锁。Core调用PHY前释放不必要的表锁，PHY不得反向调用Core。

---

## 22. 错误处理与诊断

### 22.1 返回值

公开API统一返回协议定义的UMCA错误码。平台错误在PAL或PHY边界转换，不直接穿透。

- 非零Flags返回`UMCA_ERR_UNSUPPORTED_FLAGS`。
- 非零QoS返回`UMCA_ERR_UNSUPPORTED_QOS`。
- 发现Payload字段、Boot ID或消息语义非法返回`UMCA_ERR_BAD_PAYLOAD`。
- 未实现Message Type返回`UMCA_ERR_UNSUPPORTED_TYPE`。
- 在错误生命周期调用`start/stop/register/publish`返回`UMCA_ERR_BAD_STATE`。
- MVP只在本地报告错误，不生成ERROR帧。

### 22.2 日志

Core日志是可裁剪能力：

- 默认单元测试可启用。
- Minimal Profile关闭。
- 日志只描述事件，不承担控制流。
- 不在Core中使用`printf`。
- 不记录Payload中的潜在敏感数据。

### 22.3 统计

统计用于NSH和测试，不用于协议决策。Stats关闭后所有更新宏必须为空且目标文件不参与编译。

### 22.4 断言

断言用于开发期内部不变量，不替代外部输入检查。网络输入和应用参数错误必须返回错误码，不能触发断言。

---

## 23. 构建系统设计

### 23.1 主机CMake目标

```text
umca_core_minimal
umca_core_discovery
umca_core_contest
umca_phy_loopback
umca_pal_posix
umca_unit_tests
umca_integration_tests
umca_host_three_nodes
```

不同Profile应产生不同目标，防止同一构建目录中宏配置相互污染。

### 23.2 编译选项

主机开发建议：

```text
-Wall -Wextra -Wpedantic
-Wconversion -Wsign-conversion
-Wshadow -Wundef
```

测试构建增加：

```text
AddressSanitizer
UndefinedBehaviorSanitizer
```

如openvela工具链不支持某告警，应按目标局部调整，不降低主机测试要求。

### 23.3 openvela接入

Linux Agent先检查比赛仓模板和manifest/linkfile，再选择实际接入方式。要求：

- 比赛仓中的`umca/`保持源码真源。
- openvela构建引用该目录，不复制第二份源码。
- 配置和安装步骤可以从README复现。
- 不把生成物提交为源码。

### 23.4 大小报告

每个Profile生成：

```text
size-minimal.txt
size-discovery.txt
size-contest.txt
symbols-minimal.txt
symbols-discovery.txt
symbols-contest.txt
```

关闭Discovery后，符号表不得包含`umca_discovery_*`和`umca_node_*`。

---

## 24. 测试设计

### 24.1 测试框架原则

优先使用仓库已有轻量测试设施；若没有，使用简单C测试Harness和CMake/CTest，不为MVP引入大型依赖。

### 24.2 单元测试文件

```text
test_byteorder.c
test_crc32c.c
test_frame_encode.c
test_frame_decode.c
test_topic_id.c
test_sequence.c
test_topic_table.c
test_node_table.c
test_discovery_codec.c
test_context_lifecycle.c
test_config_profiles.c
```

### 24.3 帧测试

必须覆盖：

- 空Payload。
- 256字节Payload。
- 精确296字节最大帧。
- 输出容量不足。
- Magic错误。
- Version错误。
- Header Length错误。
- Reserved非0。
- 任意非零Flags。
- QoS为1、2、3。
- Payload长度超限。
- 声明和实际长度不一致。
- CRC错误。
- Source DevID为0。
- Destination DevID为0。
- TTL为0。
- REQUEST、RESPONSE、ERROR和保留消息类型。
- 协议规范第20节固定帧向量逐字节编码和解码。

### 24.4 Sequence测试

- 第一次接收。
- 新会话首次发送Sequence为1，发送失败不递增。
- 正常递增。
- 重复帧。
- 跳号和丢包数。
- 旧帧。
- `0xFFFFFFFF → 0x00000000`。
- 半空间边界。
- CRC失败不更新Sequence。
- 相同`boot_id`继续执行正常Sequence过滤。
- 不同非零`boot_id`清除该Source的旧Sequence历史。
- 非法`boot_id`不改变旧会话状态。
- RX Sequence关闭时仍正确生成TX Sequence。

### 24.5 Discovery测试

- 合法ANNOUNCE。
- Topic count为0和8。
- ANNOUNCE恰好256字节。
- Entry截断。
- 非法方向位。
- Topic名称和ID不匹配。
- 两遍解析失败时不改变旧节点表。
- 未知DevID的HEARTBEAT被忽略且不占用节点表。
- ANNOUNCE、HEARTBEAT和TEARDOWN携带一致的非零`boot_id`。
- 同一DevID更换`boot_id`后立即进入新会话。
- 10秒STALE和15秒OFFLINE。
- TEARDOWN立即OFFLINE。
- TEARDOWN Boot ID不匹配时忽略。
- DevID冲突。
- 节点表满。
- OFFLINE表项复用且不驱逐ONLINE或STALE节点。

### 24.6 多节点集成测试

三个Context使用固定DevID和Boot ID：

```text
Sensor   DevID=0x0000000000001001 BootID=0x10010001
Agent    DevID=0x0000000000002001 BootID=0x20010001
Actuator DevID=0x0000000000003001 BootID=0x30010001
```

测试：

1. 三节点启动并互相发现。
2. Sensor发布TemperatureSample。
3. Agent缓存样本。
4. Sensor发布ThresholdEvent。
5. Agent发布FanCommand。
6. Actuator发布匹配FanState。
7. Agent确认成功。
8. 注入CRC错误、丢帧和重复帧。
9. 模拟Sensor快速重启并更换Boot ID，确认新Sequence立即被接受。

Goldfish 实测还覆盖：服务任务启动、温度周期发布、阈值事件触发、本地规则调用
`device_control`、Actuator FanState 发布和 Agent 成功确认。可通过以下命令检查
ai_agent 运行时工具：

```text
umca_agent tools
umca_agent sensor_query
umca_agent device_control on
umca_agent device_control off
```

### 24.7 资源测试

CI或本地脚本检查：

- `sizeof(umca_context_t)`。
- Profile目标文件列表。
- `.text/.rodata/.data/.bss`。
- 禁止的未实现功能符号。
- Core对象中是否存在平台专用未解析符号。

---

## 25. 性能与资源设计

### 25.1 时间复杂度

| 操作 | MVP复杂度 |
|------|-----------|
| 本地Topic查找 | O(`UMCA_MAX_TOPICS`) |
| 节点查找 | O(`UMCA_MAX_NODES`) |
| 远端Topic查找 | O(`UMCA_MAX_TOPICS_PER_NODE`) |
| 帧编解码 | O(Payload长度) |
| CRC32C | O(帧长度) |

在8节点、16本地Topic规模下，线性数组比动态哈希表更符合KISS和静态资源目标。

### 25.2 内存优化顺序

资源超标时按以下顺序处理：

1. 调小节点、Topic和队列容量。
2. 关闭Stats和Log。
3. 关闭远端Topic名称缓存或使用Minimal Profile。
4. 调小最大Payload，但不得影响需要互操作的目标Profile。
5. 评估CRC小表实现。
6. 最后才考虑压缩结构或复杂内存池。

不得首先通过未对齐packed结构换取少量RAM而增加移植风险。

### 25.3 Context预算

Core固定包含两个最大帧缓冲区，默认约592字节。其余主要占用来自节点和远端Topic名称表。

资源报告必须分别给出：

- Frame缓冲。
- 本地Topic表。
- 节点表。
- Stats。
- PHY队列。
- 应用缓存和请求表。

---

## 26. Linux Agent实施步骤

### 26.1 执行前

1. 阅读四份文档。
2. 阅读比赛仓README、AGENTS.md和提交要求。
3. 检查当前分支和工作区，但不要自动提交或推送。
4. 检查manifest/linkfile及专属仓实际映射路径。
5. 检查现有ai_agent源码和构建方式。
6. 记录发现的文档冲突。
7. 将协议规范第20节固定向量录入只读测试数据；不得由待测Codec生成期望结果。

### 26.2 第一批：只建立Core基础

创建：

```text
umca/include/umca/
umca/src/base/
umca/src/core/
umca/tests/unit/
umca/tests/vectors/
```

实现顺序：

1. 配置头和编译期检查。
2. 错误码和基础类型。
3. 字节序。
4. CRC32C并首先通过`123456789 → 0xE3069283`。
5. Topic ID并通过四个冻结Topic向量。
6. 帧Codec并逐字节通过协议完整帧向量。
7. 补充最大Payload和全部错误路径测试。

第一批不得提前实现Discovery、ai_agent或openvela适配。

### 26.3 第二批：Context与Pub/Sub

1. Context生命周期。
2. 本地Topic表。
3. TX Sequence。
4. 发布。
5. RX Sequence。
6. Dispatch。
7. 多实例测试。

### 26.4 第三批：Loopback与Discovery

1. Mock/POSIX PAL。
2. Context-aware Loopback PHY。
3. Discovery Payload Codec。
4. 节点表。
5. 定时发送和状态机。
6. 三节点主机示例。

### 26.5 第四批：openvela

1. 调查实际API。
2. 实现openvela PAL。
3. 接入构建。
4. 创建Service任务和NSH命令。
5. 在Goldfish运行三个逻辑节点。

### 26.6 第五批：ai_agent

1. 调查Tool和Message Bus接口。
2. 实现Demo消息Codec。
3. 样本缓存。
4. 请求追踪。
5. `sensor_query`。
6. `device_control`。
7. 主动事件桥和本地规则。

### 26.7 每批输出

- 修改文件列表。
- 构建命令。
- 测试命令和结果。
- 当前Profile资源报告。
- 未完成项。
- 新发现的协议或设计问题。

没有用户明确要求时，不执行`git commit`、`git push`、`git reset`或分支操作。

---

## 27. 实施门禁

### Gate A：Codec完成

- 固定向量全部通过。
- 最大帧无越界。
- Sanitizer无错误。
- 无平台头文件。

### Gate B：Core完成

- 多Context隔离。
- Pub/Sub和Sequence通过。
- 无动态内存。
- Minimal Profile裁剪有效。

### Gate C：发现完成

- 三节点发现。
- STALE、OFFLINE和TEARDOWN通过。
- ANNOUNCE事务式解析。
- 节点表满行为明确。

### Gate D：Goldfish完成

- openvela构建成功。**已完成（Loopback PHY）**。
- NSH可管理UMCA。**已完成当前 `umca_agent` 启动/诊断入口**；通用生命周期命令仍待增强。
- 三节点Demo稳定运行。**已完成**。
- 无持续刷屏的非必要服务。

### Gate E：Agent闭环完成

- Tool查询真实数据。**已完成**。
- 控制等待真实确认。**已完成**。
- 主动事件闭环。**已完成**。
- LLM失败时本地规则可用。**已完成**。

未通过当前Gate时不得开始下一阶段的大规模功能开发。

---

## 28. 已知风险与设计决策

| 问题 | 当前决策 |
|------|----------|
| PHY多实例 | 协议v0.1.2固定使用`ops + driver context`并定义无帧与MTU返回语义 |
| PAL mutex创建可能动态分配 | mutex由宿主创建并注入 |
| 快速意外重启Sequence重置 | 只有ANNOUNCE的新`boot_id`建立会话，HEARTBEAT/TEARDOWN只作用于匹配会话 |
| Topic多回调 | MVP每Context每Topic一个回调 |
| Topic查找效率 | 固定小表线性查找 |
| Core TX/RX队列 | Core不重复建队列，队列属于PHY |
| 回调内发布 | 使用独立RX/TX缓冲，允许发布 |
| 回调内修改Topic表 | MVP禁止 |
| 动态Topic | MVP禁止，全部在start前注册 |
| LLM不稳定 | 本地规则兜底，不阻塞UMCA |
| Goldfish资源数据 | 不用于GD32可行性判断 |

---

## 29. 完成定义

软件设计落地完成必须满足：

1. `umca/`是独立、自包含、可单独构建的目录。
2. Core不依赖openvela、POSIX、ai_agent或具体PHY。
3. PHY和PAL支持实例上下文或可测试上下文。
4. Context由调用者静态持有且支持多实例。
5. Core无内部线程和动态内存。
6. Minimal、Discovery、Contest Profile能独立构建。
7. 未启用模块在符号和内存报告中消失。
8. 帧、Topic、Sequence和Discovery测试通过。
9. 主机三节点示例通过Loopback完成闭环。
10. Goldfish通过openvela PAL完成相同闭环。
11. ai_agent适配不侵入UMCA Core。
12. 资源报告能独立列出UMCA、PHY、应用和ai_agent占用。
13. 非零Flags、非零QoS和未实现Message Type均被拒绝且不触发回调。
14. 编解码结果与协议规范固定向量逐字节一致。
15. OFFLINE表项可复用，错误Boot ID的TEARDOWN不能注销当前会话。

---

## 30. 最终实施原则

Linux Agent实现时应始终遵守：

1. 先测试向量，后写Codec。
2. 先主机Core，后openvela集成。
3. 先同步轮询，后平台任务封装。
4. 先固定数组，后考虑复杂容器。
5. 先实现MVP，未实现功能保持构建失败或完全缺席。
6. 先量化资源，再讨论优化。
7. 任何平台适配都不能反向污染Core。
8. 任何线级歧义都必须先修订协议文档。

该实现应以“关闭功能后代码和RAM真正消失”为可裁剪标准，以“第二个平台无需修改Core”为跨平台标准，以“非法输入永远不能到达业务回调”为协议健壮性标准。

## 31. 实现同步记录（2026-08-22）

当前代码与本设计的对应关系如下：

| 设计边界 | 当前实现 |
|----------|----------|
| UMCA Core | `umca/` 独立静态库，未包含 ai_agent/openvela 头文件 |
| Goldfish PAL/PHY | `umca_openvela_pal` + 三实例 `umca_loopback_phy` |
| 三节点服务 | `app/openvela_umca_agent/service/umca_service.c`，独立 `task_create` 服务任务 |
| Tool 适配 | `adapter/umca_agent_adapter.c`，注册 `umca` provider 并刷新工具缓存 |
| 运行入口 | `umca_agent_main`，支持 `tools`, `sensor_query`, `device_control on|off` |

验证结论：主机 CTest 3/3 通过；Goldfish 日志出现 `sensor temperature`、
`threshold event`、`actuator applied ... publish=0` 和 `agent control result={"ok":true...}`；
ai_agent Tool 实际返回 `sensor_query` 温度缓存及 `device_control` 的 `request_id/state`。
QuickApp 缺失 LFS 库和宿主 `libpulse.so.0` 是环境准备问题，不是 UMCA 运行时失败。

## 32. GD32 硬件阶段同步（2026-09-06）

已确认目标 MCU 为 GD32F470VKT6；新增 `umca/phy/uart/` 静态字节流适配器及
`umca/ports/openvela/umca_openvela_uart.*` NuttX 文件描述符桥接。主机测试覆盖：

- 任意分片边界下的完整帧重组；
- 连续两帧粘包后逐帧交付；
- 发送端短写循环；
- 无数据轮询不阻塞。

该适配器已经完成主机验证，但尚未连接 GD32 实板。硬件接口现已确定为 UART3、
PC10/TX、PC11/RX、AF8、115200 8N1、3.3 V TTL 和 `/dev/ttyS1`；实板验证仍需
串口对端和 Windows 端烧录调试记录。USART0/PB6/PB7 `/dev/ttyS0` 保持为 NSH，
SDIO 因 PC10/PC11 复用冲突而保持关闭。

Linux 当前配置的资源结果为：2026-09-08 切换 UART3 并重新全量链接后，
`umca_gd32` 固件 Flash 244,640 B、SRAM 15,920 B。历史最小 NSH 基线为
Flash 230,004 B、SRAM 6,700 B。
ELF 已包含应用入口、UART PHY、openvela UART 桥接和 Discovery ANNOUNCE 符号。

---

*本文档为UMCA初版软件详细设计v0.1.2，与架构v0.3.3、协议规范v0.1.2和开发计划v0.1.2同步，已冻结严格Flags/QoS行为、PHY/PAL实例模型、三类服务发现消息的Boot ID会话机制、统一API、节点表复用和固定符合性向量，可用于正式开始Core开发。*
