# 02 FreeRTOS 操作系统拷打

> FreeRTOS 是嵌入式面试问得最细的部分。本专题所有问题都绑定项目真实代码（`User/app.c` 的 vTaskCreateFunction、`User/FreeRTOSConfig.h`、`Driver/BSP/can.c` 中断等）。

---

## 2.0 项目任务清单（面试前必须背下来）

| 任务 | 优先级 | 栈(字) | 职责 |
|------|--------|--------|------|
| vLED1Task | 1 | 128 | 创建 LED 软件定时器后 vTaskDelete 自杀 |
| vUART1Task | 4 | 128 | 串口数据分发（任务通知唤醒） |
| vKeyScanTask | 2 | 128 | 按键扫描状态机（20ms 周期） |
| vKeyProcess | 2 | 256 | 按键事件处理（点亮/关闭背光、打印栈信息） |
| SD_Card_Init | 2 | 256 | 挂载 FatFs（一次性任务，完成后自杀） |
| BMSRecord | 3 | 512 | CAN 收包 + 协议解析 + 去重 + 分发 |
| SDCard_Write | 2 | 512 | 512B/60s 批量写 SD 卡 |
| Ymodem_Task | 3 | 512 | 串口命令行 + Ymodem 文件传输 |
| vLCDDisplayTask | 2 | 512 | 100ms 批量刷新 LCD（14 行 × 40 字符滚动） |

配置速记：抢占式、tick=1000Hz、configMAX_PRIORITIES=32、堆 30KB、软件定时器任务优先级=31、栈溢出检测关闭（手动 uxTaskGetStackHighWaterMark）。

---

## Q2.1 为什么要上 FreeRTOS？裸机能不能做？怎么判断一个项目需不需要 RTOS？

### 答题要点

- 本项目是多路异步事件并发：CAN 高频率收包（250kbps 满载约 3000 帧/秒）、SD 卡慢速写入（单次 f_write 可能阻塞几十 ms）、LCD 100ms 周期刷新、串口命令交互、按键检测、LED 闪烁——这些事件**时间尺度差 3 个数量级**。
- 裸机"超级循环 + 中断标志"方案的问题：任何一处阻塞（比如等 SD 卡编程完成）都会拖垮整个循环，CAN 中断虽然能收进 FIFO 但 3 个邮箱 + 2 个 FIFO 深度有限，主循环一卡就会丢帧。
- 用 RTOS 的本质收益是**以时间换并行**：把"不可被阻塞"的高频路径（CAN 收包）和"允许慢"的低频路径（SD 写）拆成不同任务，靠队列解耦，慢任务阻塞不影响快任务。
- 判断标准：事件源是否 ≥3 个、时序要求是否差异大、是否有长时间阻塞操作。一个只做"按键点灯"的项目上 RTOS 是过度设计。

---

## Q2.2 你的任务优先级是怎么定的？为什么 UART 任务优先级最高（4）？（高频拷打）

### 答题要点（按"约束优先"的思路答）

1. **先定约束**：CAN 是核心数据源，绝对不能因为任务调度而丢包。CAN 收包任务（BMSRecord=3）必须高于所有消费者（SDCard_Write=2、LCD=2），保证数据"被取走"的速度快于"被塞满"的速度。
2. **串口任务最高（4）**：因为 Ymodem 传输是实时交互协议——串口发来的 'C'/ACK/数据包如果处理不及时会触发对方重传甚至中断传输；而且 vUART1Task 只做"搬运"（把 DMA 缓冲转存队列），执行时间极短，优先级高也不会饿死别人。
3. **为什么 SD 写任务优先级低（2）**：写 SD 是慢操作且自身有 3 级缓冲（CAN 队列→SD 写队列→任务内 512B 累积缓冲），允许被延迟；低优先级还让它能"攒够一批再写"。
4. **同级任务靠时间片轮转**（configUSE_TIME_SLICING=1，tick=1ms）：2 优先级的 5 个任务轮流跑。
5. **LED 用软件定时器（定时器任务优先级 31）而不是任务**：LED 翻转只需要周期回调，不值得单独占用一个任务栈；vLED1Task 创建定时器后立刻自杀，把工作交给定时器服务任务。

### 追问：为什么软件定时器任务优先级是 configMAX_PRIORITIES-1（31）？

> 定时器回调必须尽量准时执行（否则 LED 会闪动不均、电池电压检测滞后），所以定时器服务任务优先级要高于所有业务任务；又留出"空闲任务"最低优先级 0 的位置。

### 追问：BMSRecord 和 Ymodem 都是 3，会互相影响吗？

> 会分时片轮转。Ymodem 任务大部分时间阻塞在 xQueueReceive 等串口字符，几乎不占 CPU；真正传输文件时 Ymodem_SendFile 会拿到 SD 互斥量占用 SD 总线，此时 SDCard_WriteTask 阻塞等互斥量，但 BMSRecord 不受影响——它只往内存队列里塞，队列深度 3 若满则打印警告丢弃，属于设计内降级。

---

## Q2.3 队列、信号量、互斥量、任务通知有什么区别？你项目里分别用在哪？

### 答题要点（背框架 + 项目实例）

| 机制 | 本质 | 项目中的应用 |
|------|------|-------------|
| 队列 Queue | 传**数据**，支持多对多，有深度和元素大小 | can1_rx_queue（10×Can1RxFrame）、xKeyQueue（10×uint8_t）、xSDCardWriteQueue（3×512B）、lcd_display_queue（512×uint8_t）、ymodem_rx_queue（256×uint8_t） |
| 二值/计数信号量 | 传**事件**（无数据），信号量可以"发布即丢" | g_sdio_transfer_sem：SDIO 驱动层等 DMA 传输完成（ISR give，任务 take） |
| 互斥量 Mutex | 传**独占权**，有优先级继承，谁拿谁还 | xSDCardMutex：保护 FatFs/SD 总线的整段文件操作；g_sdio_mutex：sd_read_disk/sd_write_disk 入口保护 |
| 任务通知 | 任务自带邮箱，**速度最快**（比信号量快约 45%，RAM 省一半），只能"通知特定任务" | USART1 空闲中断用 vTaskNotifyGiveFromISR 通知 vUART1Task，任务用 ulTaskNotifyTake 等待 |

**什么时候用哪个**（面试官最爱听这个决策逻辑）：
- 要传数据 → 队列；
- 只传事件、可能多个生产者 → 信号量；
- 保护共享资源、需要防止优先级翻转 → 互斥量（有优先级继承）；
- 单一事件、单一接收任务、要快 → 任务通知。

### 追问：互斥量和二值信号量的本质区别？优先级继承是什么？

> 二值信号量不记录"谁拥有"，任何任务/中断都能 Give，可能出现"任务 A 拿到、任务 B 释放"的语义混乱；互斥量必须**谁 Take 谁 Give**，且被占用时若更高优先级任务来抢，系统会**临时把持锁任务的优先级提升到抢锁任务的优先级**（优先级继承），防止中等优先级任务长期霸占 CPU 导致高优先级任务被"夹死"（优先级翻转）。
> 项目例子：xSDCardMutex 如果换成二值信号量，Ymodem 传输期间 SD 写任务可能错误解锁，导致两个任务同时操作 FatFs，文件系统直接损坏。

---

## Q2.4 中断里怎么和任务通信？为什么用 xQueueSendFromISR 而不是 xQueueSend？

### 答题要点
- 所有FromISR都是取消了阻塞行为 都可以这样回答  send的时候如果队列满了会阻塞 但是中断不能阻塞
- xQueueSend 会**阻塞**（队列满时等待），中断上下文不允许阻塞，否则中断延迟失控、还可能死锁（等待一个永远没人释放的条件）。
- FromISR 版本不阻塞、入队失败立即返回，并且通过第三个参数 pxHigherPriorityTaskWoken 通知"是否唤醒了更高优先级任务"，中断退出时配合 portYIELD_FROM_ISR 决定是否立即切换。
- 项目代码（can.c 的 CAN1_RX0_IRQHandler）：
  ```c
  if (xQueueSendFromISR(can1_rx_queue, &rxFrame, &xHigherPriorityTaskWoken) != pdPASS)
      can1_rx_lost_count++;      // 队列满：丢包 + 计数，绝不阻塞
  CAN_ClearITPendingBit(CAN1, CAN_IT_FMP0);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  ```
- 附加要点：清除中断标志要**在处理之后**；ISR 里只做"取帧→拷贝→入队"三件事，微秒级退出。

### 追问：为什么中断优先级必须是数值 ≥5 才能调用这些 API？
-FreeRTOS 在内核临界区里，通过写 BASEPRI = 5 来屏蔽所有「优先级数值 ≥ 5」的中断
-也就是说优先级>=5的中断 没办法打断内核操作
-因此大于等于5的中断触发的时候 系统一定不在修改内核 所以可以安全调用api
-如果中断优先级＜5 可能在系统操作内核的时候就触发中断 这个时候在中断里面调用api修改内核 会发生冲突
> 见 Q2.5。这是本项目 CAN 中断抢占优先级设为 5、USART1 设为 6、SDIO 设为 6 的根本原因。

---

## Q2.5 讲一下 FreeRTOS 在 Cortex-M4 上的中断优先级约束（configMAX_SYSCALL_INTERRUPT_PRIORITY 原理）

### 答题要点

1. **分组**：main.c 里 `NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)`，4 位优先级全给抢占优先级，无子优先级（0~15 共 16 级）。
2. **FreeRTOS 的临界区保护**：FreeRTOS 通过 BASEPRI 寄存器屏蔽"优先级数值 ≥ 某阈值"的中断来保护内核临界区。
3. **configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5**：数值 **5~15** 的中断在运行时可被 BASEPRI 屏蔽，是"FreeRTOS 管得了"的中断，可以安全调用 xQueueSendFromISR 等 API；数值 **0~4** 的中断永远不能被屏蔽，属于"内核管不了"的高优先级中断，**绝对不能**调用任何 FreeRTOS API，否则可能在内核临界区里改内核结构，直接花屏死机。
4. **项目里的落实**：CAN1_RX0=5、DMA2_Stream3=5、USART1=6、SDIO=6、RS485=5——全部落在安全区（≥5），因为 CAN 中断要入队、串口中断要发任务通知、SDIO/DMA 中断要给信号量。
5. **Cortex-M 的优先级数值是反的**：数值越小优先级越高；且 STM32 中断优先级寄存器只用高 4 位（configPRIO_BITS=4），FreeRTOS 宏里 `<< (8 - configPRIO_BITS)` 就是在做位移适配。

---

## Q2.6 讲一下 USART1 空闲中断 + 任务通知的完整链路

### 答题要点（这是项目里最能体现功底的链路之一）

1. **初始化**（usart.c USART1_Init）：USART1 115200；DMA2_Stream5_Ch4 循环模式，外设=USART1->DR（不递增），内存=dma_rx_buffer（递增）；使能 USART_DMAReq_Rx 和 IDLE 空闲中断；NVIC 抢占优先级 6。
2. **数据到达**：每个字节由 DMA 自动搬进环形缓冲，CPU 零参与。
3. **帧结束判定**：总线空闲超过 1 字节时间 → USART_IT_IDLE 置位 → USART1_IRQHandler。
4. **ISR 里做四件事**：
   - 先读 SR 再读 DR（**这个顺序不能反**，读 DR 是清除 IDLE 标志的唯二手段之一）；
   - 停 DMA，`received_len = DMA_RX_BUFFER_SIZE - DMA_GetCurrDataCounter(DMA2_Stream5)` 算出本帧长度；
   - 重置计数并重启 DMA（循环模式，后续字节继续收）；
   - `vTaskNotifyGiveFromISR(vUART1TaskHandle, &xHigherPriorityTaskWoken)` + `portYIELD_FROM_ISR`。
5. **任务侧**：vUART1Task 里 `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` 阻塞等待；被唤醒后把 dma_rx_buffer 里 received_len 字节逐字节转发到 ymodem_rx_queue 和 lcd_display_queue。
6. **为什么用任务通知**：单生产者（USART1 中断）→ 单消费者（vUART1Task），正是任务通知的最佳场景，速度比二值信号量快、RAM 开销小。

### 追问：为什么 ISR 里要先读 SR 再读 DR？

> IDLE 中断标志清除条件：读 USART_SR 寄存器之后再读 USART_DR 寄存器。顺序反了标志清不掉，会反复进中断。代码里 `volatile uint32_t tmp = USART1->SR; tmp = USART1->DR;` 就是标准写法。

### 追问：DMA 循环模式下环形缓冲会不会覆盖没处理完的数据？

> 会，这是固有风险。缓解：缓冲区 1024 字节远大于典型命令长度（几十字节），且 IDLE 中断在"帧结束"立刻唤醒任务搬走数据；若极端情况下上一帧没搬完下一帧已经覆盖，则只能接受丢数据——本场景是人工敲命令 + Ymodem 流控（对方等 ACK），实际不会发生。

---

## Q2.7 你项目里任务栈设多大？怎么确定？栈溢出怎么查？

### 答题要点

- **单位是字不是字节**：xTaskCreate 第四个参数 128/256/512 都是 Word，BMSRecord 512 字 = 2KB（栈内有大数组 ParseResult_t（约 600B）+ SDCardWriteData_t 512B + 128B LCD 缓冲，所以给大）。
- 经验初值：简单任务 128~256 字，含大局部缓冲的任务 512 字。
- **查法**：双击按键触发打印 `uxTaskGetStackHighWaterMark()`（历史上栈余量的最小值）和 vTaskList 全表；注释掉的 vTaskMonitor 任务每 5 秒打印一次。原则：高水位 < 剩余 1/4 就要加大。
- 更保险：打开 configCHECK_FOR_STACK_OVERFLOW=2（本项目关了，用运行时高水位代替）+ configASSERT。

### 追问：为什么 TaskHandle_t 变量要定义在 main.c？（项目真实注释里的坑）

> main.c 注释明确写了：`TaskHandle_t vUART1TaskHandle = NULL;//需要定义在main函数，然后其它文件用extern引用。如果定义在其他地方，会因为初始化顺序不对导致报hard错误`。
> 原因：C 运行时静态区初始化顺序由链接器决定，跨文件无保证；若某文件在 FreeRTOS 调度器启动前、句柄变量尚未初始化的窗口期使用它会拿到随机值。定义在 main.c 并赋 NULL 保证最早初始化，其他文件用 extern 引用。

---

## Q2.8 为什么 SD_Card_Init 和 vLED1Task 干完活就 vTaskDelete 自己？任务自删除安全吗？

### 答题要点

- 这两个任务是"一次性任务"：初始化/创建定时器完成后没有继续存在的意义，自删后栈和 TCB 立即归还堆（configSUPPORT_DYNAMIC_ALLOCATION=1，任务由 FreeRTOS 堆动态分配）。
- **删除任务 = 不删除任务持有的资源**：必须保证自删前把互斥量等资源释放干净。vLED1Task 自删前已 xTimerStart 把工作移交定时器服务任务；SD_Card_Init 自删前已完成 f_mount 并让 fs_obj 用 static 保持存活（否则文件系统对象被回收，后面 f_open 直接崩）。
- 扩展知识点：删除"别的任务"更危险——对方可能正拿着互斥量/处于临界区，所以最佳实践是"自删"或"让任务自己退出的信号"。

### 追问：fs_obj 为什么必须是 static？

> FATFS 结构体约 560 字节，f_mount 挂载后整个运行期都要用（FatFs 每次文件操作都引用它）。放任务栈上则任务删除后成野指针；用 static 放静态区，函数退出后依然有效。g_fs 全局指针同理（main.c 里 `FATFS *g_fs = NULL;`）。

---

## Q2.9 你的 FreeRTOS 堆多大？内存从哪来？和 my_malloc 什么关系？

### 答题要点

- configTOTAL_HEAP_SIZE = 30KB（heap_4 之类的分配器，从系统 RAM 划一块 ucHeap）。
- 任务创建、队列、互斥量、信号量、软件定时器全部从这 30KB 里动态分配。
- **两套内存体系并存**：
  - FreeRTOS 堆：内核对象（TCB、队列控制块+存储区）；
  - 正点原子 my_malloc（SRAMIN 内部 128KB SRAM）：业务大块内存，如 FATFS 结构体、FIL 结构体、Ymodem 1KB 发送缓冲。my_malloc 用"内存表 + 2 的幂分块"管理，调用 my_malloc/myfree。
- 为什么要分开：FreeRTOS 堆给内核对象保证确定性；业务大块内存（FIL 约 500B、Ymodem 缓冲 1KB）用专门池，避免碎片化互相影响。

---

## Q2.10 谈谈你对优先级翻转的理解，结合 SD 卡场景举例

### 答题要点

- 经典场景：低优先级任务 C 拿着 SD 互斥量 → 高优先级任务 A 抢 SD 锁 → 中等优先级任务 B 就绪并长期运行 → A 被 B 挡在 CPU 外、C 又拿不到 CPU 释放锁 → A 无限等待（翻转：B 优先级低于 A 却先跑）。
- 互斥量的优先级继承解决：A 抢锁瞬间，C 的优先级被临时抬到 A 的级别，C 快速跑完临界区释放锁。
- 本项目：SDCard_WriteTask(2) 持 xSDCardMutex 时，Ymodem_Task(3) 抢锁 → SDCard_WriteTask 临时升到 3 → 写完后 Ymodem 继续。所以**共享资源必须用互斥量而非二值信号量**。

---

## 面试官可能挖的坑（本专题防翻车清单）

1. **"栈 512 是字节吧？"** → 坚决纠正：字（Word），512 字 = 2KB。
2. **"为什么不用队列传通知？"** → 队列传的是数据且入队要拷贝，任务通知零拷贝、快、省 RAM，但一对一是限制。
3. **"队列满丢包，那数据不是丢了吗？"** → 承认并解释取舍：日志设备丢"最新帧"比阻塞憋死整条链路代价小；且 CAN 硬件 FIFO（2 个深度 3）+ 软件队列（10）一共约 16 帧缓冲，250kbps 下留给任务 4ms 处理窗口，足够。
4. **"vTaskDelay(100) 能精确延时 100ms 吗？"** → 不能，vTaskDelay 是相对延时且"至少"语义：唤醒时刻是 100 tick 之后的下一次调度，实际 100~101ms；要周期精确用 vTaskDelayUntil。项目里按键 20ms 扫描对精度不敏感所以用 vTaskDelay。
5. **"配置里 configCHECK_FOR_STACK_OVERFLOW=0，不怕溢出？"** → 回答：运行时用 uxTaskGetStackHighWaterMark 主动巡检（按键触发+监控任务），开发期够用；量产会打开方法 2。
6. **"为什么 tick 用 1000Hz？"** → 延时精度 1ms（Ymodem 超时、按键状态机、LCD 100ms 刷新都基于 tick），代价是每秒 1000 次 SysTick 中断，168MHz 主频下开销可忽略。
