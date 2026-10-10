---
kind: build_system
name: Keil MDK uVision 工程构建系统（ARM-ADS + STM32F407 FreeRTOS）
category: build_system
scope:
    - '**'
source_files:
    - f407_freeRTOS.uvprojx
    - f407_freeRTOS.uvoptx
    - DebugConfig/F407_freeRTOS_STM32F407ZG.dbgconf
    - DebugConfig/Target_1_STM32F407ZGTx.dbgconf
    - User/FreeRTOSConfig.h
---

## 1. 使用的构建系统

本项目使用 **Keil MDK (ARM-ADS)** 作为唯一的构建与调试工具链，通过 uVision IDE 的工程文件进行编译、链接、烧录和调试配置。没有 Makefile、Dockerfile、CI 脚本或外部构建脚本。

- 编译器：`ARMCC` V5.06 update 7（build 960），目标 CPU `Cortex-M4`，启用 FPU (`ARM_CM4F`)。
- 设备：`STM32F407ZG`，Pack ID `Keil.STM32F4xx_DFP.2.15.0`。
- 输出：可执行文件 `Charge.axf`（位于 `..\Output\`），同时生成 map、交叉引用等链接器产物。
- 调试：通过 `SARMCM3.DLL` + ST-Link（`UL2CM3.DLL`）进行在线调试/烧录。

## 2. 关键文件

- `f407_freeRTOS.uvprojx` — uVision 项目定义，包含 Target、Toolset、IncludePath、Defines、MemoryMap、Groups（源文件分组）等全部构建元数据。
- `f407_freeRTOS.uvoptx` — uVision 工作区选项（用户界面状态、打开的文件列表等）。
- `DebugConfig/*.dbgconf` — 针对不同硬件板卡（如 `F407_freeRTOS_STM32F407ZG.dbgconf`、`Target_1_STM32F407ZGTx.dbgconf`）的调试器连接配置。
- `User/FreeRTOSConfig.h` — FreeRTOS 内核配置头文件（堆大小、任务栈、Tick 频率等）。

## 3. 架构与约定

### 3.1 工程分组（Groups）
源文件按功能分层组织在 uVision Groups 中，对应目录结构：

| Group | 路径 | 内容 |
|---|---|---|
| `Driver/CMSIS` | `Driver/CMSIS/` | Cortex-M4 核心头文件、启动汇编、`system_stm32f4xx.c` |
| `Driver/FWLIB` | `Driver/FWLIB/src/` | ST 标准外设库（ADC/CAN/DMA/GPIO/I2C/SPI/TIM/USART 等） |
| `Driver/SYSTEM` | `Driver/SYSTEM/` | 延时、串口、系统初始化等基础驱动 |
| `Driver/BSP` | `Driver/BSP/` | 板级支持包（LED、OLED、SDIO、CAN、RTC、RS485、电源管理） |
| `Driver/LCD` | `Driver/BSP/LCD/` | LCD 驱动及字库 |
| `Middle/malloc` | `Middle/MALLOC/` | 内存分配器 |
| `Middle/FreeRTOS_core` | `Middle/FreeRTOS/` | FreeRTOS 核心（tasks/queue/timers/event_groups/stream_buffer/croutine/list） |
| `Middle/FreeRTOS_port` | `Middle/FreeRTOS/portable/RVDS/ARM_CM4F/` | ARM-ADS 移植层（`port.c`）+ `heap_4.c` 内存管理 |
| `Middle/FATFS` | `Middle/FATFS/` | FATFS 文件系统 |
| `User` | `User/` | 应用代码（`main.c`、`app.c`、`ymodem_protocol.c`、`BMS279.c`） |

### 3.2 预处理器宏
- `STM32F40_41xxx` — 选择 STM32F407 系列寄存器定义。
- `USE_STDPERIPH_DRIVER` — 启用 ST 标准外设库。

### 3.3 包含路径
编译器 IncludePath 顺序为：
`Driver/BSP; Driver/CMSIS; Driver/FWLIB; Driver/FWLIB/inc; Driver/SYSTEM; Middle/FreeRTOS/include; Middle/FreeRTOS/portable/RVDS/ARM_CM4F; Middle; Middle/MALLOC; Middle/FATFS; User; Driver/BSP/LCD`

### 3.4 内存布局
由 uVision 直接声明 On-Chip Memories：
- `IRAM`: `0x20000000`, 128KB
- `IRAM2`: `0x10000000`, 64KB
- `IROM`: `0x08000000`, 1MB（对应 STM32F407ZG 的 1MB Flash）
- 链接器 TextAddressRange=`0x08000000`，DataAddressRange=`0x20000000`。

### 3.5 调试器配置
`DebugConfig/` 下每个 `.dbgconf` 文件对应一块物理开发板，命名约定为 `<板卡名>_<MCU型号>.dbgconf`，例如 `F407_freeRTOS_STM32F407ZG.dbgconf`、`Target_1_STM32F407ZGTx.dbgconf`。这些文件用于切换不同的 JTAG/SWD 连接参数。

## 4. 约定与约束

- **IDE 绑定**：所有构建参数（编译器版本、优化等级、包含路径、宏定义、链接器地址范围、源文件分组）均硬编码在 `f407_freeRTOS.uvprojx` XML 中，不使用外部构建脚本；因此该工程只能在安装了 Keil MDK ARM-ADS 5.06 的环境中使用 uVision 打开并编译。
- **工具链版本锁定**：`<pCCUsed>` 字段固定为 `5060960::V5.06 update 7 (build 960)::./ARMCC`，更换编译器版本可能导致构建失败。
- **输出位置固定**：编译产物统一输出到工程目录上一级的 `..\Output\`，可执行文件名固定为 `Charge`。
- **Flash 烧录前置**：`UpdateFlashBeforeDebugging=1`，每次调试前自动将固件写入 Flash。
- **无 CI / 自动化构建**：仓库中没有 Makefile、shell 脚本、GitHub Actions / GitLab CI 配置或 Dockerfile，无法在无 GUI 环境下自动化构建。
- **FreeRTOS 移植层**：FreeRTOS 使用 RVDS（ARM Compiler 5）移植端口 `portable/RVDS/ARM_CM4F`，与 ARMCC 工具链匹配。
- **多板卡调试配置分离**：不同硬件板卡的调试器参数放在独立的 `.dbgconf` 文件中，便于在 uVision 中按需切换。
- **未启用 Hex 文件输出**：`CreateHexFile=0`，仅生成 AXF（ELF 格式）供调试器使用，不直接产出 Intel HEX 供第三方烧录工具。