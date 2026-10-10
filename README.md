# STM32 CAN Tracker

基于 **STM32F407 + FreeRTOS** 的电动汽车直流充电 CAN 总线报文记录仪：实时监听 GB/T 27930 充电协议报文，解析后在 LCD 上滚动显示，并按天记录到 SD 卡 CSV 日志，支持通过串口 Ymodem 协议将日志导出到 PC。

![MCU](https://img.shields.io/badge/MCU-STM32F407ZGTx-blue)
![Core](https://img.shields.io/badge/Core-Cortex--M4F%20168MHz-blue)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-green)
![Protocol](https://img.shields.io/badge/Protocol-GB%2FT%2027930--2015-orange)
![IDE](https://img.shields.io/badge/IDE-Keil%20MDK%20(ARM--ADS)-lightgrey)

---

## 功能特性

- **CAN 总线监听**：CAN1 @ 250kbps（GB/T 27930 规定波特率），中断收包 + FreeRTOS 队列缓冲，满队丢包计数
- **协议解析引擎**：支持 GB/T 27930-2015 全部 19 种单帧报文（CHM/BHM/CRM/CTS/CML/BRO/CRO/BCL/CCS/BSM/BMV/BMT/BSP/BST/CSD/BEM 等）及 J1939 传输协议多帧组包（BRM/BCP/BCS）
- **相同帧去重**：充电过程中周期报文（如 BCL）重复率高，与上一帧比对去重后再记录，显著降低存储与显示压力
- **SD 卡日志记录**：FatFs R0.16 文件系统，按天生成 `BMS_log_YYYYMMDD.csv`，追加写入；512 字节批量写 + 60 秒超时落盘策略，降低写放大、延长卡寿命
- **LCD 实时显示**：320×240 横屏，14 行 × 40 字符滚动窗口，100ms 批量刷新
- **串口 Ymodem 导出**：USART1 内置 shell（`help` / `ls` / `cd` / `pwd` / `get` / `settime`），`get <文件名>` 触发 Ymodem 协议（SOH 128B / STX 1024B，CRC16 校验）将日志下载到 PC
- **人机交互**：按键状态机（短按亮屏 / 双击打印任务栈水位 / 长按关背光），LED 运行指示
- **RTC 时间戳**：LSE 晶振驱动，日志时间精确到毫秒（`HH:MM:SS mmm`，亚秒计数器）
- **低功耗管理**：VBAT ADC 电压监测，低于 3.2V 自动关背光并进入 Standby（WKUP 唤醒）
- **RS485 接口**：9600bps，预留外设扩展

## 系统架构

```
CAN 总线 (250kbps, GB/T 27930)
   │ CAN1_RX0 中断
   ▼
can1_rx_queue (深度 10)
   │ BMS27930_RecordTask  优先级 3
   │  ├─ 与上一帧比对去重
   │  └─ BMS27930_ParseMessage() 协议解析 → CSV 日志行
   ├────────────────────────────┐
   ▼                            ▼
xSDCardWriteQueue (深度 3)   lcd_display_queue (深度 512)
   │ SDCard_WriteTask 优先级 2   │ vLCDDisplayTask 优先级 2
   │ 累积 512B 或 60s 超时落盘    │ 100ms 批量刷新 14×40 字符
   ▼                            ▼
sd_write_disk → SDIO+DMA     LCD 滚动显示
   ▼
SD 卡 (BMS_log_YYYYMMDD.csv)
   ▲
   │ xSDCardMutex 互斥
Ymodem_Task 优先级 3 ← ymodem_rx_queue ← vUART1Task 优先级 4
   │ get 命令触发文件导出                 (USART1 DMA 循环 + IDLE 空闲中断 + 任务通知)
   ▼
USART1 → PC (Ymodem 下载)
```

### FreeRTOS 任务一览

| 任务 | 优先级 | 栈（字） | 职责 |
|------|:---:|:---:|------|
| `vUART1Task` | 4 | 128 | 串口 DMA 接收搬运（任务通知唤醒） |
| `BMS27930_RecordTask` | 3 | 512 | CAN 报文去重、协议解析、CSV 格式化 |
| `Ymodem_Task` | 3 | 512 | 串口 shell 命令解析、Ymodem 文件导出 |
| `SDCard_WriteTask` | 2 | 512 | 512B/60s 批量写 SD 卡 |
| `vLCDDisplayTask` | 2 | 512 | LCD 滚动显示 |
| `vKeyScanTask` | 2 | 128 | 按键 20ms 轮询扫描（状态机） |
| `vKeyProcessTask` | 2 | 256 | 按键事件处理（亮屏/关屏/打印栈水位） |
| `SD_Card_Init` | 2 | 256 | SD 卡挂载与互斥量创建（完成后自删除） |
| `vLED1Task` | 1 | 128 | LED 运行指示 |

### 同步机制

| 对象 | 类型 | 用途 |
|------|------|------|
| `xSDCardMutex` | 互斥量 | 保护 `f_open → f_write → f_close` 整段磁盘操作（记录任务 / Ymodem 导出共用 SD 卡） |
| `g_sdio_mutex` | 互斥量 | 驱动层单次块传输保护 |
| `g_sdio_transfer_sem` | 二值信号量 | SDIO/DMA 中断 → 任务的软硬同步 |
| `can1_rx_queue` 等 5 个队列 | 消息队列 | 中断/任务间数据传递 |
| 任务通知 | — | USART1 IDLE 中断唤醒 `vUART1Task`（零拷贝） |

## 硬件平台

| 部件 | 规格 |
|------|------|
| 主控 | STM32F407ZGTx（Cortex-M4F，168MHz，1MB Flash，128KB SRAM + 64KB CCM） |
| CAN | CAN1 @ 250kbps（SJW=1, BS1=6, BS2=7, BRP=12，APB1=42MHz，采样点 ≈57%），FIFO0 接收，中断优先级 5 |
| SD 卡 | SDIO 4-bit 总线 + DMA2_Stream3，支持 SDSC/SDHC |
| LCD | 320×240 TFT（横屏），16px 行高 |
| 串口 | USART1 115200（DMA2_Stream5 循环接收 + IDLE 空闲中断），RS485 9600 |
| RTC | LSE 32.768kHz 晶振，亚秒计数器提供毫秒 |
| 其他 | 按键 ×3、LED、蜂鸣器、VBAT ADC 电压监测 |

## 软件栈

- **Keil MDK**（ARM Compiler 5，ARM-ADS 工具链），Device Pack：`Keil.STM32F4xx_DFP 2.15.0`
- **ST 标准外设库**（SPL，`USE_STDPERIPH_DRIVER`，非 HAL）
- **FreeRTOS**（ARM_CM4F 移植层 + heap_4，堆 30KB，tick 1000Hz，抢占式调度，`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`，NVIC 分组 4）
- **FatFs R0.16**（用户自定义 diskio 适配 SDIO 驱动）
- **自定义内存管理**（`mymalloc`，SRAMIN 内部 SRAM 池）

## 目录结构

```
CAN_Tracker/
├── Driver/                    # 底层驱动
│   ├── BSP/                   # 板级驱动：CAN、SDIO、LCD、OLED、按键/LED/蜂鸣器、RTC、RS485、电源管理
│   ├── CMSIS/                 # 内核头文件与启动代码
│   ├── FWLIB/                 # ST 标准外设库
│   └── SYSTEM/                # delay / sys / usart 基础驱动
├── Middle/                    # 中间件
│   ├── FATFS/                 # FatFs 文件系统 + diskio 适配层
│   ├── FreeRTOS/              # 内核源码 + ARM_CM4F 移植层
│   └── MALLOC/                # 自定义内存管理
├── User/                      # 应用层
│   ├── main.c                 # 入口：外设初始化、队列创建、任务创建、启动调度器
│   ├── app.c / app.h          # 各任务实现（记录、写卡、显示、按键、SD 初始化）
│   ├── BMS279.c / BMS279.h    # GB/T 27930 协议解析引擎
│   ├── ymodem_protocol.c / .h # Ymodem 传输协议 + 串口 shell
│   └── FreeRTOSConfig.h       # FreeRTOS 内核配置
├── Project/                   # Keil MDK 工程文件（f407_freeRTOS.uvprojx）
├── Output/                    # 编译产物（已 gitignore）
└── .gitignore
```

## 快速开始

### 环境要求

- Keil MDK-ARM（ARM Compiler 5，V5.06 及以上）
- Device Pack：`Keil.STM32F4xx_DFP 2.15.0`（Pack Installer 中安装）
- ST-Link / J-Link 调试器

### 编译与烧录

1. 用 Keil uVision 打开 `Project/f407_freeRTOS.uvprojx`
2. 编译（F7），产物输出到 `Output/Charge.axf`
3. 连接调试器，下载（F8）——Flash 算法 `STM32F4xx_1024.FLM`，起始地址 `0x08000000`

### 上电运行

1. 设备接入充电 CAN 总线（250kbps），插入 FAT32 格式 SD 卡
2. LCD 显示 `STM32_CAN_Tracker` 启动画面，随后滚动显示解析后的报文
3. SD 卡按天生成 `BMS_log_YYYYMMDD.csv`
4. USART1（115200，UTF-8）连接串口终端，可用 shell 命令操作：

```
help              # 查看命令列表
ls                # 列出 SD 卡文件
get BMS_log_20261011.csv   # Ymodem 下载日志到 PC（终端需支持 Ymodem 接收）
settime           # 设置 RTC 时间
```

## 关键设计说明

- **批量落盘**：SD 写入任务在 RAM 中累积一个扇区（512B）或 60 秒无新数据时才写卡，避免频繁小写入损耗 SD 卡；阻塞等待采用"距上次收数据的剩余超时"动态计算，保证超时点精确锚定
- **中断安全**：所有调用 FreeRTOS FromISR API 的中断优先级均 ≥ 5（CAN1_RX0=5、DMA2_Stream3=5、SDIO=6），符合 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` 约束；ISR 中配合 `xHigherPriorityTaskWoken` + `portYIELD_FROM_ISR` 安全切换
- **双层互斥**：应用层 `xSDCardMutex` 保护文件系统操作序列，驱动层 `g_sdio_mutex` 保护单次块传输，职责分离
- **零拷贝串口接收**：USART1 DMA 循环模式 + IDLE 空闲中断 + 任务通知，不定长命令无需逐字节中断
- **协议去重**：记录任务缓存上一帧，相同报文直接复用已格式化的日志行，减少重复解析与字符串拼接

## 已知限制

- CAN 采样点约 57%（偏早），适用于短距离低速总线，高噪声长总线场景建议调整位时序至 75%~87.5%
- `can1_rx_queue` 深度 10，总线爆发流量下可能丢包（ISR 内有丢包计数）
- SDSC v1.1 卡（≤2GB，字节寻址）的地址转换分支存在已知缺陷，建议使用 SDHC 卡

## 许可

本项目仅供学习与研究使用。
