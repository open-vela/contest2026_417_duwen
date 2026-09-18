# Windows UART/TCP 分阶段验证说明

本文档记录在真实 ESP32 接入前，以 Windows、CH340 和 TCP 对端完成的 UMCA 物理链路
验证。该前置验证已完成，实体ESP32-S3随后于2026-09-18通过最终混合实物验收。
GD32 使用 UART3 PC10/TX、PC11/RX、115200 8N1；Windows 默认使用 COM7。
Linux 负责编译，Windows 负责串口、烧录和调试。`.vscode/` 不属于本流程。

## 1. 安装依赖

在仓库根目录打开 PowerShell：

```powershell
py -m pip install -r scripts\windows\requirements.txt
py -m unittest scripts\windows\test_windows_tools.py -v
```

## 2. 第一阶段：GD32 与 Windows 原始 UMCA 帧

接线：GD32 PC10/TX 接 CH340 RX，PC11/RX 接 CH340 TX，双方 GND 共地。CH340 必须
使用 3.3 V TTL。

先观察并校验 GD32 发出的 ANNOUNCE、HEARTBEAT：

```powershell
py scripts\windows\umca_uart_test.py --port COM7 `
  --log uart-rx-evidence.jsonl monitor --seconds 40
```

再执行发送测试：

```powershell
py scripts\windows\umca_uart_test.py --port COM7 `
  --log uart-exercise-evidence.jsonl exercise
```

测试工具依次发送：

1. ANNOUNCE 和 HEARTBEAT；
2. 正常 DATA；
3. 拆成两次写入的半帧 DATA；
4. 一次写入两帧的粘包 DATA；
5. CRC 错误的 Sequence 5，随后发送合法 Sequence 5；
6. 重复 Sequence 5 和陈旧 Sequence 3；
7. 关闭并重新打开 COM7，使用新 Boot ID 重新 ANNOUNCE，再从 DATA Sequence 1 开始。

COM 端无法自动读取 GD32 内部统计，验收时需结合 NSH/GDB 确认：合法帧被接收；坏 CRC
不提交 Sequence；重复和陈旧 Sequence 被拒绝；新 Boot ID 清除旧会话 Sequence；错误码
符合预期且无 HardFault。

在没有其他 UART 对端流量且从测试前统计快照计算增量时，一次完整 `exercise` 的预期值为：

| 统计项 | 预期增量 | 说明 |
|---|---:|---|
| `rx_frames` | 13 | 包含合法帧、坏 CRC、重复帧和陈旧帧 |
| `rx_crc_errors` | 1 | 坏 CRC Sequence 5 |
| `rx_duplicates` | 1 | 重复 Sequence 5 |
| `rx_stale` | 1 | 陈旧 Sequence 3 |
| `rx_unsubscribed` | 6 | 当前 GD32 应用未注册温度订阅，合法 DATA 在路由阶段计数 |
| `node_online_events` | 1 | 首次 ANNOUNCE 上线；在线状态下切换 Boot ID 不重复计数 |
| `rx_semantic_errors` | 0 | 本组测试不发送语义错误帧 |

`umca_gd32 status` 的 `last_error` 是粘滞值；坏 CRC 用例会把它置为 `-7`
（`UMCA_ERR_CRC`），后续合法帧不会自动清零。这是负向用例的预期结果。执行
`umca_gd32 stop` 并重新 `start` 后服务对象重新初始化，才应恢复为错误码 0。

### 2.1 2026-09-09 实测结论

阶段一已通过。Windows 证据文件为 `uart-rx-evidence.jsonl` 和
`uart-exercise-evidence.jsonl`；GD32 GDB 统计为：

```text
last_error = -7
rx_frames = 13
rx_crc_errors = 1
rx_duplicates = 1
rx_stale = 1
rx_unsubscribed = 6
rx_semantic_errors = 0
node_online_events = 1
node_offline_events = 1
capacity_errors = 0
```

节点以新 Boot ID 上线，并在最后一次心跳 15 秒后转为 OFFLINE。该结果符合上表预期，
且未出现 HardFault。远端 Topic `used` 修复随后也已通过 GD32 实机回归。第二阶段不得
据此提前判定通过，仍需独立采集 COM↔TCP 实测证据。

## 3. 第二阶段：COM7 与 TCP 透明网桥

开始前通过 NSH 执行 `umca_gd32 stop`，再重新执行
`umca_gd32 start 0x4700000000000001 1`，以清零上一阶段的粘滞错误和统计。

第一轮采用 TCP Server 模式监听本机 `127.0.0.1:47000`。在 PowerShell 终端 A 执行：

```powershell
py scripts\windows\com_tcp_bridge.py --port COM7 --tcp-mode server `
  --host 127.0.0.1 --port-number 47000 --buffer-size 4096 `
  --stats-interval 2 2>&1 | Tee-Object -FilePath bridge-stage2-baseline.log
```

在 PowerShell 终端 B 运行 TCP 侧测试端点。断线时间使用 7 秒，以覆盖 GD32 的 5 秒
HEARTBEAT 周期并确保网桥产生 `offline_drop`：

```powershell
py scripts\windows\tcp_umca_test.py --host 127.0.0.1 `
  --port-number 47000 exercise --source 0x3300000000000001 `
  --boot-id 0x33000001 --disconnect-seconds 7 `
  --observe-seconds 6 2>&1 | Tee-Object -FilePath tcp-stage2-baseline.jsonl
```

TCP 测试端点会解析和验证 UMCA，以便生成验收证据；它与透明网桥是两个独立进程。
网桥本身仍不导入或调用任何 UMCA 代码。

也可作为 TCP Client：

```powershell
py scripts\windows\com_tcp_bridge.py --port COM7 --tcp-mode client `
  --host 192.0.2.10 --port-number 47000 --buffer-size 4096
```

网桥只复制字节，不导入 UMCA 帧模块，也不检查 Magic、长度、CRC、消息类型、Topic、
Sequence、Boot ID 或 request_id。TCP 分包和粘包原样映射为 UART 字节流，UMCA 接收端
负责按 Magic、长度与 CRC 重新同步。

### 3.1 断线和缓冲规则

- 两个方向各有一个启动时固定容量的缓冲；运行时不自动扩容。
- 缓冲空间不足时只接收可容纳的前缀，丢弃本次新数据的溢出部分，并累计溢出次数和
  丢弃字节数。
- TCP 断线时销毁旧 Socket，关闭并清空两个方向的应用缓冲。
- 断线期间持续读取 UART，但直接丢弃并累计 `offline_drop` 字节数。
- 重连会创建新连接代次和新缓冲，只转发重连后收到的字节。
- UART 驱动/FIFO 在断线和连接建立时执行 best-effort 清理；不保证撤回已经进入物理
  发送过程的字节。
- 网桥不区分控制命令与遥测；旧控制命令的最终重复防护由 UMCA Sequence、Boot ID 和
  request_id 负责。

运行时默认每隔 5 秒输出收发字节、缓冲溢出、断线丢弃、断线清空和串口清理失败统计；
基线命令将间隔缩短为 2 秒以便采集断线窗口。

### 3.2 基线测试预期结果

`exercise` 第一次 TCP 连接发送 ANNOUNCE、HEARTBEAT、普通 DATA、17+32 字节分片 DATA
及一次写入的双 DATA 粘包；主动断线 7 秒后，以新 Boot ID 重连并发送 ANNOUNCE、
HEARTBEAT 和从 Sequence 1 开始的 DATA。TCP→UART 总字节数固定为 493。

若测试前已按本节要求重启 `umca_gd32`，GD32 统计预期为：

| 统计项 | 预期值或增量 |
|---|---:|
| `rx_frames` | 9 |
| `rx_unsubscribed` | 5 |
| `rx_crc_errors` | 0 |
| `rx_length_errors` | 0 |
| `rx_semantic_errors` | 0 |
| `rx_duplicates` | 0 |
| `rx_stale` | 0 |
| `node_online_events` | 2 |
| `capacity_errors` | 0 |
| `last_error` | 0 |

测试端退出后继续等待至少 16 秒，`node_offline_events` 应变为 1。网桥最终统计应满足：

- `connections=2`，并在两个测试 Socket 关闭后得到 `disconnects=2`；
- `tcp->uart read=493 sent=493`；
- 两个方向 `overflow=0/0`；
- 7 秒断线窗口内 `uart->tcp offline_drop` 大于 0；
- `purge_fail=0`；
- TCP 端收到的 GD32 帧 `invalid_frames=0`、`discarded_bytes=0`。

`uart->tcp read/sent` 受 HEARTBEAT 相位影响，不规定固定总数。基线通过后再执行小缓冲
溢出、非空缓冲断线清空和旧数据不重放的专项故障注入；这些专项已在本次基线后完成。

`node_online_events` 统计成功 ANNOUNCE 引起的“非 ONLINE→ONLINE”转换，而不是单纯
统计不同 Boot ID。本基线在首次发送阶段观察 6 秒，随后断线 7 秒；从最后一次对端
HEARTBEAT 到重连已超过 10 秒，节点先转为 STALE，因此初次 ANNOUNCE 和重连 ANNOUNCE
各计一次，共 2 次。若携带新 Boot ID 的 ANNOUNCE 在节点仍为 ONLINE 时到达，它会建立
新会话并清除旧 Sequence，但不会单独增加该统计。

### 3.3 2026-09-14 基线实测结论

阶段二基线核心功能通过：

```text
GD32:
  last_error=0
  rx_frames=9
  rx_unsubscribed=5
  rx_crc_errors=0
  rx_length_errors=0
  rx_semantic_errors=0
  rx_duplicates=0
  rx_stale=0
  capacity_errors=0
  node_online_events=2
  node_offline_events=1

bridge:
  connections=2 disconnects=2
  tcp->uart read=493 sent=493 overflow=0/0
  uart->tcp read=245 sent=98 offline_drop=147 overflow=0/0
  purge_fail=0

TCP (two summaries):
  invalid_frames=0 discarded_bytes=0
```

最终 Discovery 表项为 `used=true`、`dev_id=0x3300000000000001`、
`boot_id=0x33000002`、`state=OFFLINE`，并正确保存温度 Topic `0x0fc95cb0`、Publisher
方向及名称 `/sensors/temperature`。未出现 HardFault，测试结束后已恢复运行并 detach。

后续测试固定使用 `--source 0x3300000000000001`。Boot ID 通过独立的 `--boot-id`
参数设置；本工具在重连时将初始值 `0x33000001` 递增为 `0x33000002`。Boot ID 不得拼接
或编码进 64 位 DevID。

### 3.4 固定缓冲与断线清空专项结论

固定缓冲溢出、非空缓冲断线清空和重连不重放专项测试通过：缓冲保持固定容量，溢出
按约束丢弃新数据并计数；TCP 断线后旧 Socket 和双向应用缓冲被清理，重连只转发新
连接代次的数据。未发现其他已确认的正式网桥代码问题。

专项测试同时确认以下边界：

- 应用层只能 best-effort 清理 UART 驱动和 FIFO；已经进入硬件移位寄存器的字节无法
  撤回。
- 极小缓冲下恰逢断线时，对端可能收到一个残帧并累计一次 CRC 错误。这是预期物理边界，
  不是旧应用缓冲被重放；后续帧应能通过 Magic 与 CRC 正常重新同步。
- Windows GBK 控制台显示测试脚本中文输出时可能发生编码异常，只影响日志呈现，不影响
  网桥转发字节。需要保留可读中文日志时，可先执行 `chcp 65001`，并设置
  `$env:PYTHONUTF8="1"` 后重新运行测试。

至此阶段二 Windows COM7↔TCP 功能验收完成。此处记录的是2026-09-14阶段状态；实体
ESP32-S3固件替换已于2026-09-18完成并通过最终混合实物功能验收。长期连续运行仍属于
独立可靠性测试。

## 4. 第二阶段验收

1. [通过] TCP→UART 分片发送，GD32 能重新组帧。
2. [通过] TCP→UART 一次发送多帧，GD32 能逐帧处理。
3. [通过] UART→TCP 转发 GD32 ANNOUNCE、HEARTBEAT 原始字节。
4. [通过] TCP 断线后 `offline_drop` 增加，旧应用缓冲被清空。
5. [通过] 重连使用新 Boot ID 且没有重放断线前数据。
6. [通过] 小缓冲高负载触发溢出，按约束丢弃新数据并累计统计。
7. [通过] 实体ESP32-S3已替换Windows网关Mock并完成最终混合实物功能验收；长期压力
   测试作为独立可靠性项目继续保留。

本章两阶段结果继续作为 UART↔TCP纯字节链路基线。2026-09-14后最终产品角色调整为
ESP32-S3-N16R8三串口固定路由网关：网关需要验证完整帧、按 Destination选择出口、
递减 TTL并重新计算 CRC，同时本地处理 LLM Topic，因此不再受“不得解析或修改帧”约束。
旧透明桥工具不修改，仍用于链路故障和缓冲边界回归。

GD32业务阶段改用 `umca_three_node_sim.py`通过单个 COM端口模拟 ESP32、Sensor和
Actuator三个逻辑节点。使用方法和最终接口见 `UMCA_Node_Integration_Guide.md`。

## 5. GD32三逻辑节点业务验收

2026-09-14，Windows单串口三节点模拟与 GD32 Agent实板业务闭环通过，覆盖：

1. Sensor、Actuator和 ESP32三个逻辑节点的 Discovery。
2. Sensor温度发布与 GD32终端温度查询。
3. 风扇手动控制、FanState确认和动作状态更新。
4. 阈值事件触发的自动控制。
5. LLM Mock请求/响应的 Boot ID与 request_id关联。
6. LLM有限动作经 GD32复核后生成独立 FanCommand并完成执行确认。

本轮启动命令提示 `UMCA GD32 service is already running`，没有重启服务清零统计。因此
最终统计只作为运行期间累计观测，不用于推导本轮精确帧数；所有关键错误计数均为0。
以上为2026-09-14历史三逻辑节点结果。2026-09-18已完成实体ESP32-S3、实体Actuator、
实体GD32和Windows串口Sensor的最终混合实物验收，ESP32固定路由、Wi-Fi/TLS及MiMo
云端API均通过；Sensor模拟成为最终方案，不再等待实体Sensor替换。

## 6. 最终混合实物验收

最终硬件组成如下：

```text
Windows Sensor -- USB-UART -- 实体ESP32-S3 -- UART -- 实体GD32
                                  |
                                  +-- UART -- 实体Actuator
                                  +-- Wi-Fi/HTTPS -- MiMo API
```

该阶段确认Windows Sensor以独立DevID/Boot ID发布温度和阈值Topic，实体ESP32完成固定
出口路由和LLM Topic本地处理，实体Actuator返回匹配的FanState，GD32继续执行C1可信
上下文生成、响应关联、动作白名单和最终授权。早期`umca_three_node_sim.py`仍保留为
全Mock回归工具，但不再代表最终产品节点构成。
