# GD32F470VKT6 硬件联调说明

本文档记录 UMCA 在 GD32F470V-START（MCU：GD32F470VKT6）上的 Linux 编译、Windows
烧录和调试边界。`.vscode/` 不属于本流程，本阶段不修改该目录。

## 1. 当前硬件基线

- 开发板：GD32F470V-START。
- MCU：GD32F470VKT6，Cortex-M4F，3 MB Flash、256 KB SRAM。
- 调试器：板载 GD-Link，提供 CMSIS-DAP/SWD。
- 上游 openvela 板级目录：
  `vendor/gigadevice/boards/gd32f4/gd32f470v_start/`。
- NSH 控制台：USART0，PB6/TX、PB7/RX，复用功能 AF7，115200 8N1，`/dev/ttyS0`。
- UMCA 数据链路：UART3，PC10/TX、PC11/RX，复用功能 AF8，115200 8N1，
  `/dev/ttyS1`。
- 电气标准：3.3 V CMOS TTL，收发双方共地，不启用 RTS/CTS，首轮联调不启用 DMA。

注意：当前上游 GD32F4 Kconfig 尚未提供 `GD32F470VK` 选项，因此板级 `defconfig`
暂用 `CONFIG_ARCH_CHIP_GD32F470IK=y` 复用同系列外设能力描述；实际 MCU、封装和链接
内存布局仍以 GD32F470VKT6（LQFP100、3 MB Flash、256 KB SRAM）为准，不应把该
兼容配置误认为开发板安装了 GD32F470IK。

以上分配是本项目的硬件接口基线。USART0 只用于 NSH/启动日志，UART3 只用于 UMCA
数据，二者不得复用。NuttX 在控制台重排开启时把 USART0 注册为 `/dev/ttyS0`，随后
按外设序号注册唯一的附加串口 UART3，因此 UART3 对应 `/dev/ttyS1`。

### 1.1 接线定义

| GD32F470V-START 信号 | 方向（相对 GD32） | 对端信号 | 说明 |
|---|---:|---|---|
| PB6 / USART0_TX / AF7（JP6-13） | 输出 | USB-TTL RX | NSH 与启动日志 |
| PB7 / USART0_RX / AF7（JP6-14） | 输入 | USB-TTL TX | NSH 命令输入 |
| PC10 / UART3_TX / AF8（JP6-28） | 输出 | RX | UMCA 字节流发送 |
| PC11 / UART3_RX / AF8（JP6-27） | 输入 | TX | UMCA 字节流接收 |
| GND | — | GND | 必须共地 |

板级通用 GPIO 测试接口另行固定为 PE7/JP5-18（输入）、PE8/JP5-17（输出）和
PE9/JP5-20（中断输入）。它们当前不由 UMCA 使用；该分配用于替换旧代码中的
PB0/PB1/PB2，避免占用 USB HS ULPI 和启动配置脚。

不得把上述串口引脚直接接到 ±12 V RS-232 接口；若对端是 RS-232，必须增加 3.3 V
TTL↔RS-232 电平转换器。USB-TTL 或 ESP32 对端必须工作在 3.3 V 逻辑电平，禁止向
GD32 IO 输入 5 V。连接器针号来自 `GD32F470V-START-V1.0.pdf` 的 JP6 网络标号；接线
前仍应以实物 JP6 丝印方向确认第 1 脚，避免镜像接反。

### 1.2 复用冲突

PC10/PC11 同时可作为 SDIO_D2/SDIO_D3。本项目将两脚保留给 UART3，构建脚本显式
关闭 `CONFIG_GD32F4_SDIO` 和 `CONFIG_MMCSD_SDIO`。启用 UMCA UART3 时
不得同时启用 SDIO；如后续必须使用 SD 卡，需要重新分配 UMCA 串口并同步修改代码、
构建配置和本文档。

板级保留的排针 SDIO 备选映射为 PA6/CMD、PC12/CLK、PC8-PC11/D0-D3。
这一映射不再使用带有板载负载的 PD2，但只能在停用 UART3 且切换到
168 MHz 或 240 MHz 时钟 Profile 后使用。本次 UMCA 闭环不启用该备选方案。

### 1.3 原理图复核结论

硬件配置以 `docs/GD32F470V-START-V1.0.pdf` 为唯一依据。复核结果如下：

- PA9 与 `USB_VBUS` 检测网络相连，不能继续作为 USART0_TX；PB6/PB7 是已引出的
  USART0 AF7 备用映射，且当前构建未启用 I2C0。
- PD2 经 R55（470 Ω）接入板载 USB 电源控制晶体管网络，不能作为稳定的 UART4_RX；
  因而 UMCA 改用未接板载负载的 PC10/PC11 UART3。
- PA11/PA12 分别连接 USB FS D-/D+；USB HS ULPI 还占用 PA3、PA5、PB0、PB1、PB5、
  PB10-PB13、PC0、PC2、PC3。分配扩展接口时不得把这些脚视为完全空闲 GPIO。
- PA13/PA14 和 NRST 用于板载 GD-Link/SWD；PD0/PD1 连接 25 MHz HXTAL，
  PC14/PC15 连接 32.768 kHz LXTAL，PB2 是启动配置脚。这些脚不纳入
  UMCA 扩展接口池。
- 通用 GPIO 测试设备已从 PB0/PB1/PB2 改到 PE7/PE8/PE9；三脚只连接扩展排针，
  不与当前控制台、UMCA、晶振、SWD、USB 或启动配置网络冲突。
- 板载目标 MCU 侧只有一颗用户 LED（PC6）和一颗用户按键（PA0）。原板级代码中的
  PE2/PE3/PF10 三 LED、PB14/PC13/PA0 三按键定义来自其他 EVAL 板，现已修正。
  PA0 按键为低电平有效，按键 IRQ 已改为双边沿，使按下和释放都能触发回调。
- GD32F470VKT6 的 LQFP100 只引出 GPIOA-E。原板级 SPI5 PG12/PG13/PG14 与 PI8 片选
  不适用于本板；SPI5/GD25 自动挂载保持关闭，后续若接外部 SPI 器件必须重新确定一组
  A-E 端口引脚和片选，不能直接启用旧定义。
- 原以太网 RMII 定义的 TX 引脚使用 GPIOG，在 LQFP100 上不存在；
  A/B/C 备选映射又与板载 USB HS ULPI PHY 冲突，且板上没有以太网
  PHY。当前板级 Profile 因此在编译期拒绝 ENET。
- 从 GD32F470I-EVAL 继承的 EXMC/TLI/DCI 引脚映射包含 GPIOF-I，且 V-START
  没有对应的 SDRAM、LCD 和摄像头接口，现已加入编译期拒绝，避免生成一个
  “能编译但无法接线”的固件。
- 25 MHz HXTAL、32.768 kHz LXTAL、3 MB Flash 和 256 KB SRAM 与原理图及器件型号
  一致。200 MHz 默认配置使用 PLL VCO 400 MHz、PLLP=2；PLLQ=8 输出为 50 MHz，
  当前 GD32F4 时钟代码未把 IRC48M 切换到 48 MHz 外设域，因此 200 MHz
  Profile 下会拒绝 USB FS、SDIO 和 TRNG。这些外设需改用 168 MHz 或
  240 MHz Profile。

## 2. Linux 编译

在 openvela 工作树执行：

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

参赛仓已经提供等价的一键脚本：

```bash
cd contest2026_417_duwen
BUILD_JOBS=4 ./scripts/build_gd32_umca.sh
```

脚本只修改 NuttX 的生成配置和构建产物，不修改上游板级 `defconfig` 或 `.vscode/`。
脚本会把 `ccache` 缓存默认放在 `/tmp`，也可通过 `CCACHE_DIR` 显式覆盖。
脚本同时固定 UART3、115200 8N1、无流控、无 DMA，并关闭与 PC10/PC11
冲突的 SDIO。为防止共用 SoC Kconfig 重新引入错误硬件映射，脚本还显式关闭
SPI5、ENET、EXMC/外部 RAM、TLI、DCI、USB FS/HS 和 TRNG。

主要产物位于 NuttX 工作目录，通常包括：

- `nuttx` / `nuttx.elf`：带调试信息的 ELF，供 GDB 使用；
- `nuttx.bin`：烧录到 `0x08000000` 的裸二进制；
- `nuttx.hex`：Intel HEX 格式；
- `nuttx.map`：资源和符号审查依据。

### 2.1 当前 NuttX 串口前置修复

当前工作区的 GD32F4 串口驱动在 `arm_earlyserialinit()` 中原先直接访问
`g_uart_devs[i]->priv`。未启用的串口槽位为 `NULL`，反汇编确认原实现会在启动早期
读取空指针偏移，存在 HardFault 风险。本地 `nuttx` 仓已将判断改为先检查
`g_uart_devs[i]`，行为与 STM32/AT32 等同类串口驱动一致，并通过 GD32 全量编译和
`checkpatch.sh`。该修复属于公共 NuttX 仓，不混入参赛仓提交；在上游分支包含等价
修复前，Windows 烧录必须使用 Linux 当前工作区生成的已修复固件。
当前本地 NuttX 修复提交为 `c42f0d1da26`，未推送远程。

当前 `build.sh --cmake` 能识别该板级配置，但 GD32F4 架构目录缺少 CMake 入口，
因此本板固定使用 `configure.sh + make`。该限制属于当前 openvela 构建树，不是 UMCA
Core 或 UART PHY 错误。

2026-09-09 Linux 实际构建结果：完成 UART3 实测及远端 Topic 状态修复后，固件 Flash
244,656 B、SRAM 15,920 B。相对 2026-09-08 的初始 UART3 版本（244,640 B），Flash
增加 16 B，SRAM 不变。ELF 已确认包含 `umca_gd32_main`、`umca_uart_phy`、
`umca_openvela_uart_open` 和 `umca_discovery_send_announce`。

干净构建还暴露出 NuttX `drivers_initialize.c` 在网络关闭时无条件包含 usrsock RPMsg
头文件的问题；已按实际初始化条件 `CONFIG_NET_USRSOCK_RPMSG_SERVER` 增加包含保护。
该修复不启用网络，也不改变 UMCA 协议行为。

串口驱动交叉检查还发现 UART4/UART6/UART7 的 console 索引误写为 3，以及
UART6/UART7 RX DMA 误用 TX 通道宏的复制错误。这些错误不影响当前 USART0
console + UART3 UMCA 路径，但已一并修正，避免后续扩展其他串口时踩中。

## 3. Windows 烧录

板载 GD-Link 连接 Windows 主机后，可使用 OpenOCD 的 CMSIS-DAP 接口。上游板级说明
给出的兼容基线为：

```powershell
openocd.exe -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg `
  -c "program nuttx.bin verify reset exit 0x08000000"
```

如果本机 OpenOCD 对 GD32F470 的识别或 Flash 算法不同，应以实际 OpenOCD 输出和厂商
工具为准，不能仅凭 STM32 目标脚本失败就判断固件错误。烧录前确认目标文件来自
Linux 本次构建，并记录 OpenOCD 版本、命令和校验结果。

## 4. Windows GDB 调试

先启动 OpenOCD 服务器：

```powershell
openocd.exe -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg
```

再在另一个终端连接 ELF：

```powershell
arm-none-eabi-gdb.exe nuttx.elf
```

GDB 最小流程：

```gdb
target extended-remote localhost:3333
monitor reset halt
load
break nx_start
continue
```

如果需要定位 HardFault，Linux 编译必须保留调试符号，并开启板级配置中的
`CONFIG_DEBUG_HARDFAULT`、`CONFIG_ARCH_STACKDUMP`；串口输出仍需通过已确认的控制台
连接观察。

## 5. UMCA UART PHY 状态

仓内已提供：

- `umca/phy/uart/umca_uart.c`：静态字节流组帧，支持分片、粘包和短写；
- `umca/ports/openvela/umca_openvela_uart.c`：NuttX 文件描述符读写桥接；
- `umca/tests/unit/test_uart_phy.c`：主机验证。

适配器搜索 UMCA Magic `0x55 0x4D`，读取固定 36 字节帧头和 Payload 长度，收齐完整
帧后才交付 UMCA Core；CRC、Version、Flags、QoS 和消息语义仍由 Core 校验。

2026-09-09 已完成 GD32 实板 UART3 双向互操作验收。PC10/PC11、COM7 CH340 收发、
ANNOUNCE、HEARTBEAT、CRC32C、帧长度、协议语义、节点上线和 Sequence 处理均通过，
错误码为 0，未出现 HardFault。Windows 端烧录后仍可按以下命令复核：

扩展阶段一测试随后验证了半帧、粘包、坏 CRC 后复用同一 Sequence、重复/陈旧
Sequence 和 COM 关闭重开。GD32 统计与预期完全一致：`rx_frames=13`、
`rx_crc_errors=1`、`rx_duplicates=1`、`rx_stale=1`、`rx_unsubscribed=6`、
`rx_semantic_errors=0`、`node_online_events=1`、`node_offline_events=1`、
`capacity_errors=0`；负向 CRC 用例后的粘滞错误为 `UMCA_ERR_CRC(-7)`。新 Boot ID
会话建立并在 15 秒无心跳后正常转为 OFFLINE，阶段一正式通过。

上述阶段一统计来自修复远端 Topic `used` 标志之前的固件。包含该修复及
`umca_node_get()` 逐字段复制修复的新构件随后已在 Windows 端重烧，GD32 实机确认远端
Topic 的 `used` 字段为 `true` 且无 HardFault。第二阶段 COM↔TCP 实测可以开始。

2026-09-14 第二阶段 COM7↔TCP 基线实测通过。GD32 接收 9 帧、5 个 DATA 进入未订阅
路由，CRC、长度、语义、重复、陈旧和容量错误均为 0，`last_error=0`。网桥完成
TCP→UART 493/493 字节以及 UART→TCP 98 字节转发，断线期间按约束丢弃 147 字节；
连接/断开各 2 次，无溢出、串口清理失败或 HardFault。远端节点最终正常 OFFLINE。

固定缓冲溢出及非空缓冲断线清空专项测试随后通过，未发现其他已确认的正式网桥代码
问题。极小缓冲断线时已进入 UART 硬件移位寄存器的字节无法撤回，可能造成一次 CRC
错误；该结果符合透明网桥物理边界，UMCA 接收端通过 Magic 与 CRC 重新同步。Windows
GBK 下中文输出异常只影响测试日志显示。

```text
umca_gd32 pinout
umca_gd32 start 0x4700000000000001 1
umca_gd32 status
umca_gd32 nodes
umca_gd32 topics
umca_gd32 stats
umca_gd32 query
umca_gd32 control on
umca_gd32 control off
umca_gd32 auto on
umca_gd32 chat turn fan on
```

第一条命令应显示 USART0/PB6/PB7 与 UART3/PC10/PC11 的冻结定义；第二条命令默认
打开 `/dev/ttyS1`。诊断时仍可显式覆盖设备路径：

```text
umca_gd32 start /dev/ttyS1 0x4700000000000001 1
```

实板验收顺序为：确认 `/dev/ttyS0` NSH 日志、确认 `/dev/ttyS1` 存在、UART3
双向字节收发、合法 UMCA 帧通过 CRC/Sequence/Topic 校验、错误帧被拒绝、最后进行
GD32↔ESP32 连续运行。

2026-09-14，GD32已从 Discovery外壳扩展为完整 Agent：订阅温度、阈值、风扇状态和
LLM响应，发布风扇命令和 LLM请求；NSH命令通过固定 semaphore邮箱交给唯一协议任务。
Linux Make构建成功，Flash区域256,080 B，SRAM区域17,384 B，`size`为 text 254,911 B、
data 1,168 B、bss 16,216 B。Windows烧录后已使用 `umca_three_node_sim.py`完成业务
实测：Discovery、温度查询、风扇手动控制、自动控制、LLM Mock请求/响应和动作执行均
通过。测试开始时服务已经运行，`start`返回 `UMCA GD32 service is already running`，
所以统计是服务运行期间累计值而非本轮清零值；关键错误计数均为0。该历史阶段结果不
覆盖真实ESP32三串口、Wi-Fi/TLS或MiMo Provider；这些项目已于2026-09-18在最终混合
实物拓扑中通过验收。

最终 ESP32型号为 ESP32-S3-N16R8，作为连接 GD32、Sensor和Actuator三条串口的固定
路由网关并调用MiMo API。GD32侧引脚保持 UART3 PC10/PC11不变；ESP32及其他节点接口
见 `UMCA_Node_Integration_Guide.md`。

## 6. 最终Agent固件复现与验收（2026-09-18）

Linux编译：

```bash
cd /home/duwen/openvela-contest/contest2026_417_duwen
BUILD_JOBS=4 ./scripts/build_gd32_umca.sh
```

本次结果为Flash 262,572 B、SRAM 17,668 B。最终固件已完成Windows端烧录，并在实体
GD32、实体ESP32-S3、实体Actuator和Windows串口Sensor组成的拓扑中通过功能验收。
从NSH可按以下命令复核：

```text
umca_gd32 start 0x4700000000000001 1
umca_gd32 query
umca_gd32 chat 当前温度是多少？
umca_gd32 chat turn fan on
umca_gd32 auto off
umca_gd32 agent-auto on
umca_gd32 agent-auto status
umca_gd32 stats
umca_gd32 agent-auto off
umca_gd32 auto on
```

无实体ESP32/Actuator时，历史Windows三节点Mock仍可用于回归：

```powershell
py scripts\windows\umca_three_node_sim.py --port COM7 --baud 115200 `
  --evidence gd32-final-agent.jsonl
```

最终验收已观察到Windows Sensor数据、实体ESP32 Discovery与路由、C1温度答复、真实
MiMo云端响应、`skill=sensor_query`、`skill=device_control ... confirmed`及实体Actuator
FanState确认。非零LLM status不得执行动作，agent-auto仍遵守互斥和冷却规则。终止后
执行`umca_gd32 stop`，确认服务可正常退出；长期压力与故障注入数据单独归档。
