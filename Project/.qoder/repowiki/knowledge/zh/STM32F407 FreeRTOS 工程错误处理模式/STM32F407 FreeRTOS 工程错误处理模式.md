---
kind: error_handling
name: STM32F407 FreeRTOS 工程错误处理模式
category: error_handling
scope:
    - '**'
source_files:
    - _gbk_backup/User/FreeRTOSConfig.h
    - _gbk_backup/Driver/BSP/sdio_sdcard.h
    - _gbk_backup/Driver/BSP/RTC_driver.c
    - _gbk_backup/User/BMS279.h
---

## 1. 使用的系统/方法

该 Keil MDK + STM32F407 + FreeRTOS 嵌入式工程采用**传统 C 语言返回值 + 宏常量**的错误处理模式，没有统一的异常框架、panic/recover 机制或中间件式错误传播。错误主要通过以下手段表达：
- `ErrorStatus`（来自 CMSIS/StdPeriph）枚举值 `SUCCESS` / `ERROR`。
- 外设驱动自定义的整型错误码（如 SD 卡的 `SD_Error` 枚举、BMS279 协议中的 `BMS27930_ERROR_t` 结构体字段）。
- FreeRTOS 断言钩子 `vAssertCalled` 用于运行时崩溃诊断。

## 2. 关键文件与位置

| 文件 | 作用 |
|---|---|
| `_gbk_backup/User/FreeRTOSConfig.h` | 定义 `vAssertCalled(char, int)` 钩子，将断言失败通过 `printf("Error: %s, %d\r\n", ...)` 输出到串口 |
| `_gbk_backup/Driver/BSP/sdio_sdcard.h` | 定义 `SD_Error` 枚举（含 `SD_ERASE_SEQ_ERR`、`SD_LOCK_UNLOCK_FAILED`、`SD_CC_ERROR`、`SD_GENERAL_UNKNOWN_ERROR`、`SD_CID_CSD_OVERWRITE`、`SD_AKE_SEQ_ERROR`、`SD_SWITCH_ERROR`、`SD_INTERNAL_ERROR`、`SD_ERROR`、`SD_DMA_ERROR` 等），以及 OCR 错误位掩宏（`SD_OCR_CC_ERROR`、`SD_OCR_GENERAL_UNKNOWN_ERROR`、`SD_OCR_AKE_SEQ_ERROR`） |
| `_gbk_backup/Driver/BSP/RTC_driver.c` | 使用 `ErrorStatus` 作为函数返回类型，在参数校验失败时直接 `return ERROR`，组合调用时按 `timeStatus == SUCCESS && dateStatus == SUCCESS` 判定整体成功 |
| `_gbk_backup/User/BMS279.h` | 定义 `BMS27930_ERROR_t` 结构体，包含 `error_reason`、`recv_format_error`、`other_error` 等字段以记录协议层错误原因 |
| `_gbk_backup/Driver/BSP/can.c` / `can.h` | CAN 日志条目 `CANLogEntry` 中携带错误信息（grep 匹配到的 `CANLogEntry`） |

## 3. 架构与约定

- **底层 HAL/StdPeriph 层**：统一使用 CMSIS 的 `ErrorStatus`（`SUCCESS`/`ERROR`）作为最细粒度的成功/失败标记，所有 BSP 驱动函数（如 RTC 初始化、设置时间日期）均以此返回。
- **设备驱动层**：针对具体外设扩展更丰富的错误码。例如 SDIO SD 卡驱动定义了完整的 `SD_Error` 枚举和 OCR 错误位宏，允许上层区分擦除序列错误、认证序列错误、DMA 错误等。
- **应用协议层**：BMS279 协议通过结构体字段（`error_reason`、`recv_format_error`、`other_error`）累积错误上下文，而非单一返回值。
- **运行时安全**：FreeRTOS 配置中启用 `vAssertCalled` 钩子，断言失败时打印文件名和行号到串口，作为裸机调试期的“软 panic”。

## 4. 约定与约束

- **返回值约定**：BSP 驱动函数普遍以 `ErrorStatus` 返回，调用方通过比较 `== SUCCESS` / `!= SUCCESS` 判断；参数非法时直接 `return ERROR`（见 `RTC_driver.c` 中对 NULL 指针、越界字段的检查）。
- **无统一错误类型**：工程中不存在一个跨模块共享的 error type 或错误码命名空间；每个外设（SD、RTC、BMS279、CAN）各自维护自己的错误表示方式。
- **无 try/catch 或 panic/recover**：C 标准库与 CMSIS 均未引入异常机制，错误一律通过返回值向上传播。
- **断言即诊断出口**：`vAssertCalled` 是唯一的运行时崩溃入口，实现为 `printf("Error: %s, %d\r\n", char, int)`，依赖串口输出进行定位。
- **错误码粒度差异大**：SD 卡驱动的错误码较丰富（约 15+ 种细分），而 RTC 驱动仅用 `SUCCESS`/`ERROR` 两态，BMS279 则用结构体字段承载多类错误——说明错误建模深度随外设复杂度而异，并非全仓一致。

总体而言，该工程的错误处理是**分散的、基于返回值的传统 C 风格**，没有集中式错误框架；其可靠性主要依赖各驱动自行定义错误码并通过返回值向上冒泡，配合 FreeRTOS 的 `vAssertCalled` 钩子作为最后兜底。