---
kind: configuration_system
name: Keil uVision + FreeRTOSConfig.h 嵌入式工程配置体系
category: configuration_system
scope:
    - '**'
source_files:
    - f407_freeRTOS.uvprojx
    - f407_freeRTOS.uvoptx
    - DebugConfig/F407_freeRTOS_STM32F407ZG.dbgconf
    - DebugConfig/1_STM32F407ZG.dbgconf
    - DebugConfig/1_STM32F407ZGTx.dbgconf
    - DebugConfig/F407_alone_STM32F407ZG.dbgconf
    - DebugConfig/Target_1_STM32F407ZGTx.dbgconf
    - _gbk_backup/User/FreeRTOSConfig.h
---

## 1. 使用的系统与工具

本仓库是一个 STM32F407ZG + FreeRTOS 的 Keil MDK (ARM-ADS) 工程，不存在通用的运行时配置文件（如 YAML/TOML/`.env`）。配置由三类文件共同承担：

| 层次 | 载体 | 作用 |
|---|---|---|
| IDE / 构建层 | `f407_freeRTOS.uvprojx`、`f407_freeRTOS.uvoptx` | Keil uVision 项目定义：目标芯片、编译器版本、输出目录、链接选项、调试器 DLL、Flash 驱动等 |
| 调试器层 | `DebugConfig/*.dbgconf` | ST-Link/J-Link 调试会话配置（DBGMCU_CR、APB1/APB2 冻结位） |
| 运行时层 | `_gbk_backup/User/FreeRTOSConfig.h` | FreeRTOS 内核行为宏（调度策略、堆大小、钩子、中断优先级、MPU 等） |

没有发现任何应用级的运行时配置加载逻辑（无 `application.properties`、无环境变量解析、无 feature flag 系统）。

## 2. 关键文件

- `f407_freeRTOS.uvprojx` — 主工程文件，定义 TargetName=`F407_freeRTOS`、Toolset=`ARM-ADS`、Device=`STM32F407ZG`、PackID=`Keil.STM32F4xx_DFP.2.15.0`、Cpu=`IRAM(0x20000000,0x20000) IRAM2(0x10000000,0x10000) IROM(0x08000000,0x100000)`、OutputDirectory=`..\Output\`、OutputName=`Charge`、FlashDriverDll=`UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32F4xx_1024 -FS08000000 -FL0100000 -FP0($$Device:STM32F407ZG$Flash\STM32F4xx_1024.FLM))`。
- `f407_freeRTOS.uvoptx` — Keil 工作区/用户偏好（未展开）。
- `DebugConfig/F407_freeRTOS_STM32F407ZG.dbgconf` — 调试寄存器初始化：`DbgMCU_CR = 0x00000007`（启用 DBG_SLEEP/DBG_STOP/DBG_STANDBY），`DbgMCU_APB1_Fz = 0x00000000`，`DbgMCU_APB2_Fz = 0x00000000`。
- `DebugConfig/1_STM32F407ZG.dbgconf`、`1_STM32F407ZGTx.dbgconf`、`Target_1_STM32F407ZGTx.dbgconf`、`F407_alone_STM32F407ZG.dbgconf` — 多套调试器配置，按不同硬件板型或调试目标区分。
- `_gbk_backup/User/FreeRTOSConfig.h` — FreeRTOS 编译期配置头文件。

## 3. 架构与约定

### 3.1 构建期配置（Keil uVision）

- 所有编译器/链接器/调试器参数集中在 `.uvprojx` XML 中，通过 `<TargetArmAds>`、`<TargetCommonOption>`、`<DllOption>`、`<Utilities>` 等节点组织。
- 设备相关路径使用 Keil 变量语法（`$$Device:STM32F407ZG$...`），依赖 DFP Pack 安装位置。
- 输出产物统一放在 `..\Output\`，可执行文件名固定为 `Charge`。
- Flash 烧录使用 UL2CM3.DLL + STM32F4xx_1024.FLM，地址映射为 `0x08000000` 起始、1MB 容量。

### 3.2 调试期配置（ST-Link/J-Link）

- 每个硬件变体一份 `.dbgconf`，通过注释块 `// <h> ... </h>` 暴露给 Keil 配置向导。
- 默认启用调试休眠/停止/待机模式（`DbgMCU_CR = 0x00000007`），但冻结所有定时器/看门狗/RTC 外设（APB1/APB2 冻结寄存器均为 0）。

### 3.3 运行时配置（FreeRTOS）

`FreeRTOSConfig.h` 采用 **纯宏开关** 风格，全部通过 `#define` 控制内核行为，无运行时 API 读取。核心约定：

- 调度：抢占式（`configUSE_PREEMPTION=1`）、时间片（`configUSE_TIME_SLICING=1`）、tickless 关闭（`configUSE_TICKLESS_IDLE=0`）。
- 时钟：`configCPU_CLOCK_HZ = SystemCoreClock`，`configTICK_RATE_HZ = 1000`。
- 任务：最多 32 个优先级（`configMAX_PRIORITIES=32`），空闲任务栈 128 字，任务名最大 16 字符。
- 内存：动态分配开启（`configSUPPORT_DYNAMIC_ALLOCATION=1`），静态分配关闭；堆总大小 `30*1024` 字节。
- 同步原语：互斥量、递归互斥量、计数信号量、队列集均开启。
- 钩子：idle/tick/malloc-failed/daemon-task 钩子全部关闭，仅保留断言钩子 `vAssertCalled`。
- 统计：运行时间统计关闭，可视化跟踪开启（`configUSE_TRACE_FACILITY=1`）。
- 软件定时器：开启，优先级 `configMAX_PRIORITIES-1`，队列长度 5，栈深度 `MINIMAL_STACK_SIZE*2`。
- 中断优先级：最低 15，Syscall 最高 5，通过 `__NVIC_PRIO_BITS`（缺省 4）计算绝对值。
- 可选 API：几乎所有 `INCLUDE_xxx` 宏都设为 1，以暴露完整 API 集合。

## 4. 约定与约束

- **设备型号硬编码**：`.uvprojx` 中 `<Device>` 和 `<Cpu>` 字段直接写死 `STM32F407ZG`，切换芯片需要手工修改工程文件。
- **FreeRTOS 配置集中化**：所有内核行为通过单一头文件 `FreeRTOSConfig.h` 中的宏控制，遵循 FreeRTOS 官方约定的命名空间（`config*`、`INCLUDE_*`、`portCONFIGURE_*`）。
- **调试配置与硬件绑定**：`DebugConfig/` 下每块板一个 `.dbgconf`，通过文件名区分目标（`*_STM32F407ZG.dbgconf` vs `*_STM32F407ZGTx.dbgconf`）。
- **无运行时配置加载**：工程中未发现任何从文件/env/flash 读取配置的代码；所有“配置”在编译期或 IDE 启动时确定。
- **无 feature flag 机制**：功能开关通过 C 预处理器宏（`configUSE_*`、`INCLUDE_*`）实现，而非运行时布尔标志。
- **备份目录约定**：源码位于 `_gbk_backup/`，表明该目录是 GBK 编码下的原始备份，当前活动工程引用其内容。

## 5. 不适用项

- 未发现 `.yaml` / `.toml` / `.env` / `application.properties` 等通用配置文件。
- 未发现运行时配置解析库或自定义配置加载函数。
- 未发现密钥管理、远程配置、OTA 配置更新等机制。
