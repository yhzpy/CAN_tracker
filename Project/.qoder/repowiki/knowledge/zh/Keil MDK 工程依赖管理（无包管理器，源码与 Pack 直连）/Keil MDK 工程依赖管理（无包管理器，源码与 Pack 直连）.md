---
kind: dependency_management
name: Keil MDK 工程依赖管理（无包管理器，源码与 Pack 直连）
category: dependency_management
scope:
    - '**'
source_files:
    - f407_freeRTOS.uvprojx
    - f407_freeRTOS.uvoptx
    - _gbk_backup/Driver/CMSIS/stm32f4xx.h
    - _gbk_backup/Driver/CMSIS/system_stm32f4xx.c
    - _gbk_backup/Middle/FATFS/diskio.c
    - _gbk_backup/Driver/BSP/can.c
    - _gbk_backup/Driver/BSP/OLED.c
    - _gbk_backup/User/FreeRTOSConfig.h
---

## 概述

本仓库是一个 STM32F407 + FreeRTOS 的 Keil MDK (ARM-ADS) 嵌入式工程，**不使用任何现代语言包管理器**（无 `go.mod`、`package.json`、`CMakeLists.txt`、`Makefile`、`vcpkg.json`、`conanfile.*` 等）。依赖通过以下三种方式声明：

1. **Keil Device Pack**（设备支持包）——由 `.uvprojx` 中的 `<PackID>` 指定。
2. **FreeRTOS 内核源码**——以 C 源文件形式直接纳入工程目录。
3. **第三方库源码**——以 C 源文件形式直接纳入工程目录（如 FATFS、BSP 驱动）。

## 关键文件

- `f407_freeRTOS.uvprojx`：Keil uVision 工程定义，声明工具链、设备、Pack、编译/链接选项。
- `f407_freeRTOS.uvoptx`：uVision 工作区/分组选项。
- `DebugConfig/*.dbgconf`：各调试器配置文件（ST-Link / J-Link），不管理依赖。
- `_gbk_backup/Driver/`、`_gbk_backup/Middle/`、`_gbk_backup/User/`：被注释或备份的源码目录，包含 BSP、CMSIS、FWLIB、FATFS、MALLOC 等第三方/中间层源码。

## 依赖来源与版本约定

### 1. Keil Device Pack

在 `f407_freeRTOS.uvprojx` 中：

```xml
<PackID>Keil.STM32F4xx_DFP.2.15.0</PackID>
<PackURL>http://www.keil.com/pack/</PackURL>
```

- 使用 Keil 官方 Pack 索引 `http://www.keil.com/pack/` 拉取 `STM32F4xx_DFP`。
- 固定版本为 `2.15.0`。
- 该 Pack 提供 CMSIS 头文件、启动代码、Flash 算法（`$$Device:STM32F407ZG$Flash\STM32F4xx_1024.FLM`）和 SVD 描述（`$$Device:STM32F407ZG$SVD\STM32F40x.svd`）。

### 2. FreeRTOS

FreeRTOS 内核以源码形式存在于工程中（例如 `User/FreeRTOSConfig.h` 以及可能的 `Source/` 目录，尽管当前根目录未列出），没有通过任何包管理器安装。版本信息仅体现在源码本身，工程文件中无显式版本号声明。

### 3. 其他第三方库

- **FATFS**：位于 `_gbk_backup/Middle/FATFS/diskio.c`，源码直接纳入工程。
- **BSP 驱动**（LCD/OLED/RS485/CAN/RTC/SDIO/Power Management）：位于 `_gbk_backup/Driver/BSP/`。
- **CMSIS**：位于 `_gbk_backup/Driver/CMSIS/`（`stm32f4xx.h`、`system_stm32f4xx.c`）。
- **FWLIB**：位于 `_gbk_backup/Driver/FWLIB/src/stm32f4xx_qspi.c`。
- **用户应用**：位于 `_gbk_backup/User/`（`main.c`、`app.c`、`BMS279.c`、`ymodem_protocol.c` 等）。

这些源码均作为普通 C 文件被编译器直接编译，不存在锁文件或版本约束文件。

## 架构与约定

- **源码即依赖**：所有第三方库均以“拷贝进仓库”的方式管理，属于典型的“vendored source”模式，但仓库内未见统一的 `vendor/` 目录，而是按功能分置于 `Driver/`、`Middle/`、`User/` 子目录。
- **无私有注册表**：Keil Pack 仅指向官方地址 `http://www.keil.com/pack/`，未发现自定义 Pack 服务器或代理配置。
- **无更新脚本**：仓库中没有用于自动升级依赖的 CI 任务、脚本或 Makefile；依赖升级需手动在 Keil MDK 中更新 Pack 并同步源码。
- **Git 跟踪**：仓库根存在 `.git/`，说明源码级依赖随 Git 版本控制一起提交。

## 约束与规则

- 设备支持包必须为 `Keil.STM32F4xx_DFP.2.15.0`（由 `.uvprojx` 中的 `<PackID>` 强制锁定）。
- 目标设备固定为 `STM32F407ZG`（Vendor=`STMicroelectronics`，Toolset=`ARM-ADS`，CPU=`IRAM(0x20000000,0x20000) IRAM2(0x10000000,0x10000) IROM(0x08000000,0x100000)`）。
- 输出产物名为 `Charge`，生成于 `..\Output\`。
- 未使用任何包管理器（npm/yarn/pip/go/vcpkg/conan/CMake FetchContent 等），因此不存在 lockfile、`GOPRIVATE`、`.npmrc` 等机制。
- 第三方库版本不可通过命令自动升级，需人工维护源码副本与 Keil Pack 版本的一致性。