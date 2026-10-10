# STM32 CAN Tracker 面试模拟题库（README）

> 面向嵌入式软件工程师面试的"面试官视角"模拟拷打题库。
> 所有问题与答案均基于本项目（STM32F407 + FreeRTOS + CAN + GB/T 27930 + SD 卡）真实代码细节，
> 保证你被追问到函数名、变量名、数值时能对得上自己的项目。

## 文档清单与阅读顺序

| 文档 | 内容 | 建议优先级 |
|------|------|-----------|
| [01_项目介绍与整体架构](./01_项目介绍与整体架构.md) | 必考题：自我介绍项目、系统架构、数据流、难点亮点 | ★★★★★ 先背 |
| [02_FreeRTOS操作系统拷打](./02_FreeRTOS操作系统拷打.md) | 任务划分、优先级、队列/信号量/互斥量/任务通知、中断安全 | ★★★★★ |
| [03_CAN总线与GB27930协议拷打](./03_CAN总线与GB27930协议拷打.md) | CAN 原理、波特率计算、过滤器、27930 报文解析、J1939 组包 | ★★★★★ |
| [04_SD卡存储与FatFs拷打](./04_SD卡存储与FatFs拷打.md) | SDIO 初始化、DMA、SDHC 地址转换、双层锁、512B 批量写策略 | ★★★★☆ |
| [05_串口DMA与Ymodem拷打](./05_串口DMA与Ymodem拷打.md) | DMA 循环接收+空闲中断、Ymodem 协议、RS485 | ★★★★☆ |
| [06_工程细节与综合拷打](./06_工程细节与综合拷打.md) | 低功耗、按键状态机、RTC、内存管理、调试手段、开放设计题 | ★★★☆☆ |

## 项目一句话定位（先记住）

> 一个挂接在**电动汽车直流充电 CAN 总线**上的**报文监听记录仪**：
> CAN 中断收包 → GB/T 27930-2015 协议解析 → LCD 实时滚动显示 + SD 卡按天记录 CSV 日志 → 串口 Ymodem 协议把日志导出到 PC。

**核心链路**（面试时随手画在白板上）：

```
CAN总线(250kbps)
   │ CAN1_RX0 中断
   ▼
can1_rx_queue（FreeRTOS队列,深度10,元素=Can1RxFrame）
   │ can1_receive_msg()
   ▼
BMS27930_RecordTask（优先级3）── 与上一帧比对去重 ── BMS27930_ParseMessage() 协议解析
   │                                              │
   ▼                                              ▼
xSDCardWriteQueue（深度3,每项512B）        lcd_display_queue（512B）
   ▼                                              ▼
SDCard_WriteTask（优先级2）                vLCDDisplayTask（优先级2）
512字节或60s批量写FatFs                     100ms批量刷新14行×40字符
   ▼
sd_write_disk → SDIO+DMA → SD卡（BMS_log_20261009.csv）

串口侧：USART1+DMA循环+空闲中断 → vUART1Task(任务通知) → ymodem_rx_queue
        → Ymodem_Task（help/ls/cd/pwd/get/settime）→ get 触发 Ymodem 传输日志文件到 PC
```

## 关键数值速查表（面试前 10 分钟过一遍）

| 项目 | 值 | 出处 |
|------|-----|------|
| 主控 | STM32F407ZGTx，Cortex-M4F，168MHz | main.c |
| 库 | SPL 标准外设库（非 HAL） | Driver/FWLIB |
| CAN1 波特率 | 250kbps = 42MHz / ((1+6+7)×12)，参数 `can1_init(1,6,7,12,Normal)` | main.c:50 |
| CAN 过滤 | 掩码模式 32 位全 0 = 不过滤，接 FIFO0 | can.c:84-93 |
| CAN 收包路径 | CAN1_RX0_IRQHandler → `xQueueSendFromISR` → 队列深度 10，满则丢包计数 | can.c:186-224 |
| CAN 中断优先级 | 抢占优先级 5（数值≥5 才能安全调用 FreeRTOS FromISR API） | can.c:97 |
| 27930 报文 | 19 种单帧（CHM/BHM/CRM/CTS/CML/BRO/CRO/BCL/CCS/BSM/BMV/BMT/BSP/BST/CST/BSD/CSD/BEM/CEM）+ J1939 TP 多帧（BRM/BCP/BCS） | BMS279.h |
| 任务数 | 9 个任务 + 1 个软件定时器 | app.c:529-541 |
| FreeRTOS | 抢占式、tick=1000Hz、configMAX_PRIORITIES=32、堆 30KB | FreeRTOSConfig.h |
| 任务栈单位 | **字（4字节）**，BMSRecord 512 字 = 2KB | app.c 注释 |
| SD 写策略 | 队列深度3；任务内 512B 缓冲，满 512B 或 60s 无数据才落盘 | app.c:313-366 |
| SD 文件名 | `BMS_log_YYYYMMDD.csv`，FA_OPEN_APPEND 追加 | app.c:44 |
| SD 地址换算 | `if (CardType != STD_CAPACITY_SD_CARD_V1_1) lsaddr <<= 9` | sdio_sdcard.c:1432 |
| SDIO 中断 | SDIO=6、DMA2_Stream3=5 | sdio_sdcard.c:71-78 |
| 串口接收 | USART1 115200，DMA2_Stream5_Ch4 循环模式 + IDLE 空闲中断 | usart.c:101-166 |
| 时间戳 | `HH:MM:SS mmm`，毫秒来自 RTC 亚秒计数器 | RTC_driver.c:198 |
| 按键 | 20ms 轮询状态机；长按 800ms；双击窗口 300ms | LedKeyBeep.h |
| 低功耗 | VBAT ADC 检测 <3.2V → 关背光 → Standby（WKUP 唤醒） | power_mgmt.c |

## 面试策略建议

1. **第一题（介绍项目）决定面试走向**：主动把"数据流 + 三个亮点"抛出来，引导面试官往你准备过的地方问：
   - 亮点1：SD 卡写入的"512 字节/60 秒批量写"策略（写放大优化，见文档 04）
   - 亮点2：协议解析的"相同帧去重"（充电过程 BCL 等周期报文重复率高，见文档 03）
   - 亮点3：串口"空闲中断 + 任务通知"的零拷贝设计（见文档 05）
2. **答数值类问题必须准确**：波特率、队列深度、优先级、栈大小这些数字被追问的概率极高，答错会翻车，答对非常加分。
3. **被问"为什么"时用"问题→方案→对比"三段式**：先说这个场景有什么问题，再说我怎么解决的，最后说为什么不选别的方案。
4. **不要背答案原句**，把每个问题下面的"答题要点"用自己的话组织，否则追问细节时会露馅。
5. 每个文档的"面试官可能挖的坑"小节是重点中的重点——那些是面试官专门用来识别"背项目"的人的问题。

## 代码行号对照（追问时快速定位）

- 任务创建表：`User/app.c` vTaskCreateFunction（529-541 行）
- SD 批量写：`User/app.c` SDCard_WriteTask（313-366 行）
- CAN 接收与去重：`User/app.c` BMS27930_RecordTask（201-309 行）
- CAN 中断：`Driver/BSP/can.c` CAN1_RX0_IRQHandler（186-224 行）
- 协议分发：`User/BMS279.c` BMS27930_ParseMessage（42-114 行）
- J1939 组包：`User/BMS279.c` Parse_1939（557-708 行）
- SD 地址转换：`Driver/BSP/sdio_sdcard.c` sd_read_disk/sd_write_disk（1421-1467 行）
- 串口空闲中断：`Driver/SYSTEM/usart.c` USART1_IRQHandler（185-205 行）
- Ymodem 发送主流程：`User/ymodem_protocol.c` Ymodem_SendFile（303-428 行）
