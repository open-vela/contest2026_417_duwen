# GD32F470VKT6 硬件联调说明

本文档记录 UMCA 在 GD32F470V-START（MCU：GD32F470VKT6）上的 Linux 编译、Windows
烧录和调试边界。`.vscode/` 不属于本流程，本阶段不修改该目录。

## 1. 当前硬件基线

- 开发板：GD32F470V-START。
- MCU：GD32F470VKT6，Cortex-M4F，3 MB Flash、256 KB SRAM。
- 调试器：板载 GD-Link，提供 CMSIS-DAP/SWD。
- 上游 openvela 板级目录：
  `vendor/gigadevice/boards/gd32f4/gd32f470v_start/`。
- 默认 NSH 控制台：USART0，115200 8N1，PA9/TX、PA10/RX。

NSH 控制台和 UMCA 数据 UART 不应在没有确认的情况下复用。实际 UMCA 对端（例如
ESP32 或 USB-TTL）、USART 实例、引脚、电平和波特率需要在上板前明确。

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

主要产物位于 NuttX 工作目录，通常包括：

- `nuttx` / `nuttx.elf`：带调试信息的 ELF，供 GDB 使用；
- `nuttx.bin`：烧录到 `0x08000000` 的裸二进制；
- `nuttx.hex`：Intel HEX 格式；
- `nuttx.map`：资源和符号审查依据。

当前 `build.sh --cmake` 能识别该板级配置，但 GD32F4 架构目录缺少 CMake 入口，
因此本板固定使用 `configure.sh + make`。该限制属于当前 openvela 构建树，不是 UMCA
Core 或 UART PHY 错误。

2026-09-06 Linux 实际构建结果：最小 NSH 基线 Flash 230,004 B、SRAM 6,700 B；
启用 `umca_gd32` 后 Flash 243,760 B、SRAM 15,232 B，增量分别为 13,756 B 和
8,532 B。ELF 已确认包含 `umca_gd32_main`、`umca_uart_phy`、
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

当前已完成主机测试，尚未完成 GD32 实板 UART 互操作。上板前需要确认：

1. UMCA UART 使用 USART0、USART3 还是其他实例；
2. 该串口是否与 NSH 控制台复用；
3. 对端设备和线序（GD32↔ESP32 或 GD32↔USB-TTL）；
4. 电平标准、波特率和是否需要流控；
5. GD32 在 UMCA 中承担 Sensor、Actuator、Agent 还是纯透传角色。

在以上信息确认前，本仓只推进编译、烧录基线和协议适配器验证，不硬编码具体硬件
角色或串口设备路径。
