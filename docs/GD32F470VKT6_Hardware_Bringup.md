# GD32F470VKT6 硬件联调说明

本文档记录 UMCA 在 GD32F470V-START（MCU：GD32F470VKT6）上的 Linux 编译、Windows
烧录和调试边界。`.vscode/` 不属于本流程，本阶段不修改该目录。

## 1. 当前硬件基线

- 开发板：GD32F470V-START。
- MCU：GD32F470VKT6，Cortex-M4F，3 MB Flash、256 KB SRAM。
- 调试器：板载 GD-Link，提供 CMSIS-DAP/SWD。
- 上游 openvela 板级目录：
  `vendor/gigadevice/boards/gd32f4/gd32f470v_start/`。
- NSH 控制台：USART0，PA9/TX、PA10/RX，115200 8N1，`/dev/ttyS0`。
- UMCA 数据链路：UART4，PC12/TX、PD2/RX，复用功能 AF8，115200 8N1，
  `/dev/ttyS1`。
- 电气标准：3.3 V CMOS TTL，收发双方共地，不启用 RTS/CTS，首轮联调不启用 DMA。

注意：当前上游 GD32F4 Kconfig 尚未提供 `GD32F470VK` 选项，因此板级 `defconfig`
暂用 `CONFIG_ARCH_CHIP_GD32F470IK=y` 复用同系列外设能力描述；实际 MCU、封装和链接
内存布局仍以 GD32F470VKT6（LQFP100、3 MB Flash、256 KB SRAM）为准，不应把该
兼容配置误认为开发板安装了 GD32F470IK。

以上分配是本项目的硬件接口基线。USART0 只用于 NSH/启动日志，UART4 只用于 UMCA
数据，二者不得复用。NuttX 在控制台重排开启时把 USART0 注册为 `/dev/ttyS0`，随后
按外设序号注册唯一的附加串口 UART4，因此 UART4 对应 `/dev/ttyS1`。

### 1.1 接线定义

| GD32F470V-START 信号 | 方向（相对 GD32） | 对端信号 | 说明 |
|---|---:|---|---|
| PC12 / UART4_TX / AF8 | 输出 | RX | UMCA 字节流发送 |
| PD2 / UART4_RX / AF8 | 输入 | TX | UMCA 字节流接收 |
| GND | — | GND | 必须共地 |

不得把 PC12/PD2 直接接到 ±12 V RS-232 接口；若对端是 RS-232，必须增加 3.3 V
TTL↔RS-232 电平转换器。USB-TTL 或 ESP32 对端必须工作在 3.3 V 逻辑电平，禁止向
GD32 IO 输入 5 V。开发板排针的物理孔位以实物丝印和原理图为准，本文冻结的是 MCU
端口号与复用功能，不在缺少原理图编号证据时猜测连接器针号。

### 1.2 复用冲突

PC12 同时可作为 SDIO_CLK，PD2 同时可作为 SDIO_CMD。本项目将两脚保留给 UART4，
构建脚本显式关闭 `CONFIG_GD32F4_SDIO` 和 `CONFIG_MMCSD_SDIO`。启用 UMCA UART4 时
不得同时启用 SDIO；如后续必须使用 SD 卡，需要重新分配 UMCA 串口并同步修改代码、
构建配置和本文档。

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
脚本同时固定 UART4、115200 8N1、无流控、无 DMA，并关闭与 PC12/PD2 冲突的 SDIO。

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

2026-09-06 Linux 实际构建结果：最小 NSH 基线 Flash 230,004 B、SRAM 6,700 B；
启用 UART4 和 `umca_gd32` 后 Flash 244,648 B、SRAM 15,920 B，增量分别为
14,644 B 和 9,220 B。ELF 已确认包含 `umca_gd32_main`、`umca_uart_phy`、
`umca_openvela_uart_open` 和 `umca_discovery_send_announce`。

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

当前已完成主机测试和 UART4 固件配置，尚未完成 GD32 实板 UART 互操作。Windows
端烧录后建议依次执行：

```text
umca_gd32 pinout
umca_gd32 start 0x4700000000000001 1
umca_gd32 status
```

第一条命令应显示 USART0/PA9/PA10 与 UART4/PC12/PD2 的冻结定义；第二条命令默认
打开 `/dev/ttyS1`。诊断时仍可显式覆盖设备路径：

```text
umca_gd32 start /dev/ttyS1 0x4700000000000001 1
```

实板验收顺序为：确认 `/dev/ttyS0` NSH 日志、确认 `/dev/ttyS1` 存在、UART4
双向字节收发、合法 UMCA 帧通过 CRC/Sequence/Topic 校验、错误帧被拒绝、最后进行
GD32↔ESP32 连续运行。GD32 的业务角色仍由后续应用配置决定，不影响本次 UART
物理接口冻结。
