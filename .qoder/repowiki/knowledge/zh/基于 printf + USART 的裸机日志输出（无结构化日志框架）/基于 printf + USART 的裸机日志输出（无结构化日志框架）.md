---
kind: logging_system
name: 基于 printf + USART 的裸机日志输出（无结构化日志框架）
category: logging_system
scope:
    - '**'
source_files:
    - gbk_backup/Driver/SYSTEM/usart.c
    - gbk_backup/User/app.h
    - gbk_backup/User/FreeRTOSConfig.h
    - gbk_backup/Driver/BSP/RTC_driver.c
    - gbk_backup/User/ymodem_protocol.c
---

## 1. 使用的系统/方法

本仓库是一个 STM32F407 + FreeRTOS 的 Keil uVision 工程，**没有引入任何第三方日志库或结构化日志框架**。日志输出完全依赖标准 C 库的 `printf`，并通过重定向到 USART 串口实现。

- 日志框架：无（仅使用 `stdio.h` 的 `printf` / `snprintf`）
- 日志级别：无统一级别定义；所有输出均为普通文本字符串
- 结构化字段：无；日志为纯文本行，格式由调用方自行拼接
- Sink（输出目标）：USART 串口（通过 `Driver/SYSTEM/usart.c` 提供的底层发送函数间接输出）

## 2. 关键文件

- `gbk_backup/Driver/SYSTEM/usart.c` — 串口驱动，提供 `printf` 重定向所需的底层字符发送实现
- `gbk_backup/User/app.h` — 声明了全局缓冲 `char log_buffer[512]`，用于临时存储日志内容
- `gbk_backup/User/FreeRTOSConfig.h` — 在 `vAssertCalled` 中直接调用 `printf` 输出断言信息
- `gbk_backup/Driver/BSP/RTC_driver.c` — 多处使用 `printf` 输出 RTC 初始化、错误和当前时间等调试信息
- `gbk_backup/User/ymodem_protocol.c` — 使用 `printf` 输出 YModem 传输进度、错误和目录列表

## 3. 架构与约定

- **输出路径**：业务代码 → `printf` → 标准库 `__io_putchar`（由 usart.c 重定向）→ USART 硬件寄存器
- **缓冲区**：`app.h` 中的 `log_buffer[512]` 作为共享日志缓冲区，供上层模块暂存待输出的日志文本
- **FreeRTOS 集成**：断言失败时通过 `vAssertCalled` 直接 `printf` 报错，未做异步队列或环形缓冲
- **无日志级别**：代码中没有 `LOG_DEBUG/INFO/WARN/ERROR` 之类的宏或枚举，也没有条件编译开关控制不同级别的输出
- **无结构化字段**：日志消息是硬编码的 C 字符串，字段名不内嵌在消息体中，无法被外部解析器按字段提取

## 4. 观察到的约定与约束

- 所有调试输出均使用 `printf("...\r\n", ...)` 形式，换行符固定为 `\r\n`（Windows 风格），这是仓库中各 `.c` 文件中 `printf` 调用的统一格式。
- 错误类输出通常以 `"Error: ..."` 前缀开头（见 `RTC_driver.c`、`ymodem_protocol.c`），便于在串口终端中快速识别异常。
- 进度类输出使用回车不换行的方式（如 `ymodem_protocol.c` 中的 `printf("\rTransferring... %lu bytes", total_sent);`）实现原地刷新。
- 断言处理在 `FreeRTOSConfig.h` 中通过 `#define vAssertCalled(char, int) printf("Error: %s, %d\r\n", char, int)` 实现，将断言参数直接打印到串口。
- 不存在统一的日志初始化入口；`printf` 的重定向在 `usart.c` 中完成，其他模块直接调用即可。

**结论**：该仓库采用最简化的“printf + USART”日志方案，没有日志框架、级别管理、结构化字段或异步写入机制。日志能力等同于裸机开发中的串口调试输出。