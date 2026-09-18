# UMCA 可移植节点开发指南

## 1. 适用范围

本文面向 Sensor、Actuator、ESP32-S3 网关及其他 UMCA 节点开发者，说明如何把平台无关
UMCA Core 接入裸机或 RTOS 工程。线级行为以 `UMCA_Protocol_Specification_v0.1.2.md`
为最高依据，比赛节点身份、Topic、Payload 和接线以 `UMCA_Node_Integration_Guide.md`
为准。本文与上述文档冲突时，不得自行推断，应先由系统集成人确认。

当前交付是 C99 静态内存 MVP，不包含分段、RPC、ACK/ERROR、QoS-1/2/3、加密、ACL、
动态路由或动态内存。ESP32 固定路由是应用层功能，不属于通用 UMCA Core。

## 2. 交付内容

| 目录 | 用途 |
|------|------|
| `include/umca/` | 公共 API、协议常量、配置和错误码 |
| `src/` | 平台无关 Core，依赖标准 C、PAL 和 PHY接口 |
| `phy/uart/` | UART字节流适配器，处理半帧、粘包和短写 |
| `phy/loopback/` | 主机和本地集成测试使用的多实例 Loopback PHY |
| `ports/posix/` | POSIX时间 PAL参考实现 |
| `demo/` | 比赛业务 Topic、ID及固定 Payload Codec |
| `tests/` | 固定向量、Core、UART和三节点集成测试 |
| `tools/windows/` | Python线码、串口和模拟节点参考工具 |
| `docs/` | 协议、软件设计、节点接口和符合性说明 |

## 3. Profile选择

| Profile | 适用场景 | 约束 |
|---------|----------|------|
| `MINIMAL` | 身份和对端均静态配置的极小节点 | 无 Discovery、远端节点表、RX Sequence跟踪和 Stats |
| `DISCOVERY` | 单协议任务拥有 Context的 Sensor/Actuator | 支持发现、Sequence和 Stats；比赛叶节点默认选择 |
| `CONTEST` | 多任务可能访问同一 Context | 功能同 Discovery，必须注入真实互斥锁 |

不要仅因为硬件资源紧张就选择 `MINIMAL`。最终比赛拓扑要求 ANNOUNCE、HEARTBEAT、
Boot ID会话和接收 Sequence防护时，应使用 `DISCOVERY`或`CONTEST`。

## 4. 平台接入

### 4.1 PAL

每个 Context持有独立 `umca_platform_t`：

- `time_ms()`必须返回单调递增的无符号32位毫秒计数，允许自然回绕。
- `CONTEST`必须提供 `mutex_lock()`、`mutex_unlock()`和有效 `mutex`对象。
- 当前 Core不调用动态分配接口；`alloc/free`应保持为空，不能借此引入堆依赖。
- 中断临界区、日志和断言回调按平台需要提供，不能改变线级语义。

### 4.2 UART PHY

优先复用 `phy/uart/umca_uart.c`，平台只实现 `umca_uart_io_t`：

- `read()`必须非阻塞；无数据时返回0并设置`received=0`。
- `write()`允许短写，但成功时必须有进展；适配器会循环直至整帧送出。
- 收发错误返回非0，不得把错误伪装成零长度成功。
- UART ISR只进入驱动 FIFO或固定队列，不在中断中调用 `umca_poll()`或 Topic回调。
- 缓冲必须有固定上限；满时记录并显式丢弃，不得覆盖未处理数据。

报文型 PHY应让一次 `send()`和一次非空 `poll()`分别对应一个完整 UMCA帧。MVP不支持
超过底层 MTU后的自动分段。

## 5. 初始化与任务模型

节点按以下固定顺序启动：

1. 初始化时钟、串口和固定收发队列。
2. 构造 PAL、PHY和唯一非零 DevID/Boot ID。
3. 调用 `umca_init()`。
4. 在 `umca_start()`前注册本节点全部 Publisher/Subscriber Topic。
5. 调用 `umca_start()`，允许 Core发送 ANNOUNCE。
6. 由唯一协议任务周期调用 `umca_poll()`，并在同一任务处理应用发送队列。
7. 停止时依次调用 `umca_stop()`、`umca_deinit()`和 PHY/硬件清理。

推荐结构：

```c
static umca_context_t g_ctx;
static umca_uart_t g_uart;

int node_protocol_init(uint64_t dev_id, uint32_t boot_id,
                       const umca_platform_t *platform,
                       const umca_uart_io_t *uart_io)
{
  umca_phy_t phy;
  int ret = umca_uart_init(&g_uart, uart_io);
  if (ret != 0)
    {
      return ret;
    }

  phy = umca_uart_phy(&g_uart);
  ret = umca_init(&g_ctx, dev_id, boot_id, platform, &phy);
  if (ret != UMCA_OK)
    {
      return ret;
    }

  /* Register every local Topic here before umca_start(). */
  return umca_start(&g_ctx);
}
```

应用任务不得绕过邮箱/队列并发调用单任务 `DISCOVERY` Context。Topic回调应快速校验并
复制必要数据，不执行阻塞串口、网络、Flash或执行器操作。

## 6. 身份、会话和Sequence

- DevID是稳定的64位设备身份，`0`非法，广播固定为`0xFFFFFFFFFFFFFFFF`。
- Boot ID是每次启动或新会话变化的独立非零32位值，禁止拼入 DevID。
- Core分别维护系统 Sequence和各 Publisher Topic的 DATA Sequence。
- 接收端只在完整帧通过 Magic、Version、Flags、QoS、长度、CRC和语义校验后更新状态。
- 业务命令还必须使用 `request_id`去重；LLM响应使用
  `requester_boot_id + request_id`关联。

Boot ID的生成必须在目标平台设计中明确。若没有可靠随机源，应使用持久启动计数器或
由可信启动方分配，不能每次固定为同一常量。

## 7. 业务节点要求

### 7.1 Sensor

- DevID固定为`0x0000000000001001`。
- 发布`/sensors/temperature`和`/events/temperature/threshold`。
- 温度默认每1000 ms发送；多字节字段使用网络字节序。
- 质量无效时发送`quality=0`，不得沿用旧样本并标记有效。
- 直连 ESP32时初始 TTL为8；由网关转发到 GD32后应变为7。

### 7.2 Actuator

- DevID固定为`0x0000000000003001`。
- 订阅`/actuators/fan/command`，发布`/actuators/fan/state`。
- 只接受来源为 GD32 DevID的合法命令，并按`request_id`防止重复物理执行。
- 执行完成或明确失败后返回相同`request_id`；不得提前报告成功。
- 重启后更换 Boot ID，不恢复或重放旧控制命令。

### 7.3 ESP32-S3网关

- DevID固定为`0x3300000000000001`。
- 三条节点链路相互独立，不能并联 TX/RX。
- 按固定 DevID出口转发完整有效帧，TTL减1并重新计算 CRC；不发回入口。
- 网络、TLS和 LLM任务不能阻塞串口路由任务。
- `/llm/chat/response`只允许无动作或设置风扇，GD32仍是最终控制授权点。
- 已验收的MiMo端点、模型、JSON Schema和凭据格式由ESP32应用配置管理；其他节点不得
  猜测或复制Provider实现，API Key不得写入节点源码、日志或SDK归档。

## 8. 符合性门禁

节点交付前至少完成：

1. `MINIMAL`、`DISCOVERY`、`CONTEST`中实际使用的 Profile能无警告构建。
2. CRC32C、Topic ID和固定帧向量与协议文档一致。
3. UART半帧、粘包、噪声后 Magic重同步和发送短写通过。
4. ANNOUNCE、HEARTBEAT、15秒离线和新 Boot ID会话通过。
5. 坏 CRC、非法长度、Flags、QoS、类型、重复和陈旧 Sequence不会进入回调。
6. 固定队列溢出可观测，长时间运行无堆增长、栈溢出或 HardFault。
7. Actuator按节点对接文档完成实体UART互操作；比赛最终Sensor由Windows串口模拟，
   但仍必须通过相同帧、Discovery、Boot ID和Topic符合性门禁。
8. 记录固件哈希、构建配置、DevID、Boot ID、串口参数、统计和故障注入结果。

## 9. 交付检查单

节点开发者提交以下信息后才进入系统集成：

- MCU/模组和板卡型号、工具链版本、RTOS或裸机版本。
- TX/RX/GND、电平、波特率、UART格式和引脚复用表。
- 固件、ELF/Map、SHA-256和 Flash/RAM/最大栈数据。
- DevID来源、Boot ID生成方式和全部 Topic方向。
- 正常、重启、断线、坏帧、重复命令和缓冲溢出测试记录。
- 未实现能力和已知硬件边界。

任何与协议、节点身份、Topic/Payload、ESP32出口表或安全权限有关的不确定项，都必须在
编码前提交系统集成人确认，不能通过兼容性猜测或静默降级处理。
