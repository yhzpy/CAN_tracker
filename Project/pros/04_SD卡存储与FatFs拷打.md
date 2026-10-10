# 04 SD 卡存储与 FatFs 拷打

> 存储是嵌入式面试的深水区：SDIO 初始化、DMA、地址换算、文件系统、写放大优化，每一个都能往下挖三层。
> 代码依据：`Driver/BSP/sdio_sdcard.c`（1469 行）、`Middle/FATFS/diskio.c`、`User/app.c` 的 write_to_sd / SDCard_WriteTask。

---

## 4.0 速记：存储链路全景

```
SDCard_WriteTask(优先级2)
  └─ 任务内 accumulate_buffer[512]：攒满512B 或 60s超时 → write_to_sd()
       ├─ xSDCardMutex（应用层互斥量，保护整段文件操作）
       ├─ f_open(FA_OPEN_APPEND|FA_WRITE, "BMS_log_YYYYMMDD.csv")
       ├─ f_write → f_close（f_write 内部→ FatFs → disk_write → sd_write_disk）
       └─ xSemaphoreGive
            └─ sd_write_disk：g_sdio_mutex（驱动层互斥量）→ 地址换算(CardType!=V1.1 则<<9)
                 → sdmmc_write_blocks → 配置DPSM → CPSM发CMD25 → 等 g_sdio_transfer_sem
                 → SDIO_IRQHandler/DMA2_Stream3_IRQHandler 释放信号量
```

关键数字：SDIO 挂 APB2（48MHz）、NVIC：SDIO=6、DMA2_Stream3=5、传输超时 3000ms、4 位宽总线。

---

## Q4.1 讲一下 SD 卡的初始化流程（你驱动里的 11 步）

### 答题要点（sdio_sdcard.c 注释里明确写了 11 步，按顺序背）

1. 配置 SDIO 时钟、GPIO（PC8~PC12、PD2）、NVIC（SDIO=6、DMA2_Stream3=5）；
2. `sdmmc_power_on()`：SDIO_ClockCmd 使能，输出 74 个时钟脉冲让卡进入工作状态；
3. CMD0（GO_IDLE_STATE）：复位卡到 Idle；
4. CMD8（SEND_IF_COND，带 0x1AA 参数）：检查 SD 2.0 接口条件，有 R7 响应说明是 SD 2.0；
5. 循环 CMD55（APP_CMD 前缀）+ ACMD41（SD_SEND_OP_COND，带 HCS 位）：查询卡是否完成上电初始化，OCR 的 busy 位拉高即就绪；HCS=1 支持高容量，OCR 的 CCS 位区分 SDHC/SDXC；
6. CMD2（ALL_SEND_CID）：读 128 位 CID（厂商、序列号）；
7. CMD3（SEND_REL_ADDR）：卡发布 RCA 相对地址（SD 卡参数传 0 让卡自选），进入 Stand-by；
8. CMD9（SEND_CSD，参数=RCA<<16）：读 CSD，解析容量（SDHC 容量 = (C_SIZE+1)×512KB）、块长、擦除参数；
9. CMD7（SELECT_CARD，参数=RCA<<16）：选中卡进入 Transfer 态，之后才能读写；
10. ACMD6（SET_BUS_WIDTH=2）：SDIO 总线切换到 4 位宽，吞吐×4；
11. 按卡类型设置 SDIO_CLKCR 分频，把时钟从初始 400kHz 提到高速（≤25MHz）。

### 追问：为什么一开始时钟要低（400kHz）？什么时候提频？

> SD 规范要求初始化阶段时钟 ≤400kHz（部分老卡只支持低速初始化）；等 CMD7 选中卡、读 SCR/CSD 确认卡支持高速后，再把 SDIO 分频改小提频。项目里 SD 1.x 卡用 SDIO_TRANSFER_CLK_DIV+2，SDHC 用 SDIO_TRANSFER_CLK_DIV 的差异化分频。

### 追问：为什么发 ACMD 前要先发 CMD55？

> ACMD（Application Command）不是独立命令，是"借道"CMD 通道的应用命令：先发 CMD55 声明"下一条命令按 ACMD 解释"，效果只对紧邻下一条有效。所以 ACMD41 的循环里每轮都要重发 CMD55。

### 追问：74 个时钟是干什么的？

> SD 卡上电后内部要完成初始化，规范要求供电后主机先发至少 74 个时钟周期，让卡进入稳定工作状态（SD 模式下 CS 拉低、SPI 模式下也有对应要求）。项目 power_on 阶段做了时钟输出。

---

## Q4.2 SDIO 读写的 DMA 是怎么配置的？为什么数据宽度用 Word？

### 答题要点（sdmmc_dma_config）

- DMA2_Stream3 + Channel4（SDIO 专用映射），外设地址 = `&SDIO->FIFO`（固定不递增），内存地址递增。
- 外设/内存数据宽度都配 **Word（4 字节）**：因为 SDIO 的 FIFO 是 32 位宽的，DMA 每次从 FIFO 读/写必须 32 位对齐，字节/半字宽度会出错。
- DMA_BufferSize 配 0：传输长度由 SDIO 数据长度寄存器（DLEN）控制，DMA 只做搬运，SDIO 数据结束产生 DMA 完成。
- 方向：读卡 = 外设→内存（DMA_DIR_PeripheralToMemory），写卡 = 内存→外设。
- 模式 Normal（单次），优先级 VeryHigh（SD 数据流不能断，FIFO 溢出/欠载会报数据错误）。
- 使能传输完成中断（DMA_IT_TC），DMA2_Stream3_IRQHandler 里置完成标志、给 g_sdio_transfer_sem 信号量。

### 追问：为什么不用 FIFO 模式/Burst？

> 简洁性优先：SDIO FIFO 本身的半满/全满中断已由 SDIO 外设管理，DMA 侧开 FIFO 会引入额外延迟且需要维护突发对齐；Single 传输在 48MHz APB2 下带宽足够（25MB/s 级别远大于 SD 卡实际速度）。复杂化没有收益就不加。

---

## Q4.3 讲讲 SDIO 的 CPSM/DPSM 双状态机，为什么写块时要先配数据路径再发命令？

### 答题要点（本项目记忆库里的架构知识，SDIO 最硬核的问题）

- SDIO 外设内部有两条独立状态机：
  - **CPSM**（命令路径状态机）：负责 CMD 线的命令/响应收发；
  - **DPSM**（数据路径状态机）：负责 DAT 线的数据收发（配合 DMA/FIFO）。
  - 两者可独立使能，互不阻塞。
- **初始化顺序约束**：在 sdmmc_read_blocks / sdmmc_write_blocks 里，必须**先配置并使能 DPSM**（SDIO_DPSM_Enable，配好块大小、块数、方向、超时），**再使能 CPSM**（SDIO_CPSM_Enable 发 CMD17/18/24/25）。
- 原因：卡一旦收到读/写命令会立刻开始在 DAT 线上送/取数据，如果 DPSM 没提前就绪，数据到达时没有状态机接管 → 数据超时/CRC 错误。命令是"开闸"，数据路径必须提前"站好位"。
- 项目代码在读写函数里正是这个顺序：sdmmc_send_data_cfg → SDIO_DPSM_Enable → sdmmc_send_cmd（CPSM 使能）。

---

## Q4.4 FatFs 是什么？你的分层结构是怎样的？

### 答题要点

- FatFs 是专为嵌入式设计的开源 FAT/exFAT 文件系统（ChaN 编写），模块化、RAM 占用小，移植只需实现底层 5 个函数：disk_initialize / disk_status / disk_read / disk_write / disk_ioctl。
- 项目分层（自顶向下）：
  ```
  应用（write_to_sd：f_open/f_write/f_close，互斥量保护）
    ↓
  FatFs 核心（ff.c：FAT 表管理、目录、文件分配）
    ↓
  diskio.c 适配层（disk_read/disk_write → sd_read_disk/sd_write_disk）
    ↓
  SDIO 驱动（sdmmc_read_blocks/sdmmc_write_blocks + DMA + 中断信号量）
    ↓
  硬件（SDIO 外设 → SD 卡）
  ```
- 关键设计：**diskio 适配层只管扇区级 I/O，不含任何 FreeRTOS 知识**；并发控制放在两个边界上——应用层 xSDCardMutex（文件级）+ 驱动层 g_sdio_mutex（扇区级），见 Q4.6。

### 追问：f_mount 里的参数 1 是什么意思？f_open 返回 FR_NO_FILE 你怎么处理？

> f_mount(fs, "0:", 1) 的第三个参数是 opt，1 表示"延迟挂载失败也立即尝试挂载"。FR_NO_FILE 处理：write_to_sd 里第一次 f_open 用 FA_OPEN_APPEND，返回 FR_NO_FILE 说明当天文件还没建，改用 FA_CREATE_NEW|FA_WRITE 创建。

---

## Q4.5 为什么要做"512 字节/60 秒批量写"？直接每帧 f_write 会怎样？

### 答题要点（项目最大的存储亮点，讲透）

**每帧直接写的代价：**
1. **写放大**：FatFs 一次 f_write 至少写 1 个扇区（512B）。一帧日志约 40~80 字节，写 512B 意味着"为了 40 字节搬 512 字节"，10 倍放大；
2. **元数据更新放大**：f_open（目录项）/ f_close（FAT 链 + 文件大小 + 目录项）每次都要**读-改-写** FAT 扇区和目录扇区，一次 40 字节的追加背后是 3~4 次扇区级操作；
3. **SD 卡编程时间**：NAND 页编程 + 擦除管理，单次小写入耗时 1~10ms 不可控，期间任务被阻塞；
4. **寿命**：SD 卡 NAND 擦写次数有限（MLC 约 3000~10000 次），充电 4 小时按 50ms 一帧 ≈ 28 万帧 ≈ 上百万次扇区写，卡会提前报废。

**我的方案（两级缓冲）：**
- 第一级：BMS27930_RecordTask 把日志放入 xSDCardWriteQueue（深度 3，每项 512B）；
- 第二级：SDCard_WriteTask 内部 accumulate_buffer[512] 累积，**攒满 512 字节（恰好一个扇区）或 60 秒无新数据**才调用一次 write_to_sd；
- write_to_sd 内部 f_open(APPEND) → f_write → f_close，整个操作互斥量保护。

**效果：** 写次数降约 2 个数量级；512 字节整扇区写对 FatFs 更友好；60 秒兜底保证日志延迟最多 1 分钟（掉电最多丢最近 60 秒，可接受并明确告知面试官这是权衡）。

### 追问：60 秒超时怎么实现的？为什么不用软件定时器？

> SDCard_WriteTask 里用 xQueueReceive 的**可变超时**：`wait_time = 60s - (now - last_receive_time)`，收到数据就重置计时，超时返回则把残留缓冲刷盘。不用软件定时器是因为"超时触发刷盘"本身就是这个任务的阻塞点，复用队列接收超时最简洁、少一个跨任务同步。

### 追问：为什么队列深度是 3？每项为什么 512B？

> 每项 512B = 一个扇区，正好与批量写粒度对齐（SDCardWriteData_t.log_buffer[512]）；深度 3 = 1.5KB 内存占用与缓冲能力的平衡——CAN 满载时日志生成速率约几十 KB/s，队列深度 3 大约能抗 100ms 的写盘阻塞窗口，够用且不浪费 RAM。

---

## Q4.6 你的"双层同步机制"是怎么回事？为什么分两层？

### 答题要点（架构题，直接背这个框架）

| 层 | 对象 | 作用域 | 类型 | 解决的问题 |
|----|------|--------|------|-----------|
| 应用层 | xSDCardMutex（app.c 创建）+ g_sdio_mutex（驱动内） | write_to_sd 整段文件操作 / sd_read_disk、sd_write_disk 入口 | 互斥量 | **任务间竞争**：SDCard_WriteTask 写日志 vs Ymodem_Task 读文件导出，谁先拿到谁用整条 SD 链路 |
| 驱动层 | g_sdio_transfer_sem | sdmmc_read/write_blocks 内部等待 DMA 完成 | 二值信号量 | **软硬同步**：CPU 发出命令后阻塞，等 SDIO/DMA 中断释放信号量才继续 |

设计原则：**上层管"谁用卡"，下层管"传完没"**。
- 互斥量有优先级继承，适合资源独占；
- 二值信号量是"事件通知"语义，且在中断里 Give 安全；
- 分层后故障定位边界清晰：文件乱 → 查应用层锁；传输超时 → 查驱动层信号量。

### 追问：为什么中断里不能直接 Give 互斥量？

> 互斥量含优先级继承逻辑，Give 必须由持有者（任务）执行，中断上下文没有"持有者"概念，语义非法；且中断里操作互斥量可能导致优先级继承状态破坏。信号量（尤其 FromISR 版本）才是中断→任务通知的正确工具。

---

## Q4.7 SDHC 卡的扇区地址为什么要左移 9 位？（经典坑）

### 答题要点（sd_read_disk/sd_write_disk 1432/1460 行）

```c
if (CardType != STD_CAPACITY_SD_CARD_V1_1) lsaddr <<= 9;
```

**协议规范（先背这个）**：
- **SDSC（标准容量，≤2GB）**：CMD17/18/24/25 参数是**字节地址**；
- **SDHC/SDXC（高容量，≥4GB）**：参数是**块地址**（512B 为一单位）。

**代码执行流（结合两层驱动讲）**：
- FatFs 传下来的是 **LBA 扇区号**；
- 上层 sd_read_disk/sd_write_disk 做第一次换算，下层 sdmmc_read_blocks/sdmmc_write_blocks 内部对高容量卡还会右移 9 位还原成块号，两层要合起来看：
  - **SDHC/SDXC**：上层不移位（保持扇区号），下层右移 9 位还原 → 最终传给 CMD 的是块号 ✓
  - **SDSC v2.0**：上层左移 9 位（扇区号→字节地址），下层不动 → 最终是字节地址 ✓
  - **SDSC v1.1**：上层因 `CardType == STD_CAPACITY_SD_CARD_V1_1` 也不移位、下层也不动 → 扇区号被当成字节地址直传 ✗ **寻址偏差 512 倍**（潜在 Bug，v1.1 老卡会读写错位）

**主动亮出对代码的批判（加分项，展示你审查过自己的代码）**：
1. 条件写反了：正确的写法应该是 `if (CardType != HIGH_CAPACITY_SD_CARD) lsaddr <<= 9;`，即"所有非高容量卡都左移"，这样 v1.1 才不会被漏掉；
2. 代码里那句注释"高容量SD卡使用字节地址"本身是**错的**——事实恰恰相反（高容量用块地址、标准容量用字节地址），所以不能盲信注释，要拿协议文档交叉验证。

**调试方法**：逻辑分析仪抓 CMD 线看 CMD17/24 参数；或发 CMD13 读卡状态看 ADDRESS_ERROR 位；对照验证：同位置连续读两次结果不一致、写后读回内容错位 512 倍，都是地址换算问题的典型症状。

### 追问：SDHC 容量怎么从 CSD 算出来？

> CSD v2.0 的 C_SIZE 是 22 位，容量 = (C_SIZE + 1) × 512KB（代码：`CardCapacity = (DeviceSize + 1) * 512 * 1024`）。CSD v1.0 则按 C_SIZE、C_SIZE_MULT、READ_BL_LEN 三个字段计算：容量 = (C_SIZE+1) × 2^(C_SIZE_MULT+2) × 2^READ_BL_LEN。

---

## Q4.8 为什么 SD 卡写要用互斥量？如果两个任务同时写会怎样？

### 答题要点

- FatFs **不是线程安全的**（Reentrancy 默认关）：FAT 表、目录缓存、文件对象（FIL）都是共享状态，并发 f_write/f_open 会破坏 FAT 链 → 文件损坏、交叉写入、甚至整个分区不可读。
- 本项目的两个并发写读者：SDCard_WriteTask（写日志）+ Ymodem_Task（get 命令读文件下载，也访问同一文件系统）。二者都先 `xSemaphoreTake(xSDCardMutex, ...)` 再操作、操作完 Give。
- 互斥量（而非关中断临界区）的原因：文件操作耗时几十 ms，临界区会长时间禁调度、丢 CAN 包；互斥量让等待方"睡在队列上"，CPU 让给别人。

### 追问：Ymodem 传输期间日志写入会被饿死吗？

> 会排队但不会死锁：Ymodem_SendFile 在等待接收端 'C' 的 30 秒窗口里**主动释放互斥量**（代码里"释放SD卡互斥量"注释处），读文件数据前再重新获取——长等待期间日志照写。这是对"锁粒度"的刻意设计。

---

## Q4.9 掉电瞬间正在写 SD 卡会怎样？怎么改进？

### 答题要点

- 风险点：扇区写一半掉电 → 该扇区损坏；FAT 表更新一半 → 文件链断裂（"文件存在但读不出"）；目录项没写完 → 文件丢失。
- 本项目现状：512B/60s 批量写已把窗口压缩到最小，但 write_to_sd 没有 f_sync（FatFs 自带写缓冲），数据可能停在 FatFs 内部缓冲没落盘。
- 改进方向（主动说）：
  1. 每次 f_write 后 f_sync 强制刷盘（牺牲吞吐换可靠性）；
  2. 硬件加掉电检测（VBAT ADC 已有雏形）→ 检测到低压立刻停止新写入、完成当前扇区、f_close；
  3. 双文件轮换 + 环形日志，损坏只影响一段而非整个文件。

---

## 面试官可能挖的坑（本专题防翻车清单）

1. **"SDHC 地址是扇区号直接传吗？"** → 按项目驱动约定：接口是字节地址，SDHC/SDXC 要 ×512（<<9），只有 STD_CAPACITY_SD_CARD_V1_1 不乘。别答反。
2. **"f_write 是原子操作吗？"** → 不是。f_write 会拆多次扇区 I/O，中途被打断数据不一致，必须靠互斥量保护整段"open-write-close"。
3. **"你测过写速度吗？"** → 建议提前实测并记住：4 位总线 24MHz 理论 12MB/s，实际连续写约 2~5MB/s（受卡内部编程时间支配），随机小写会掉到百 KB/s——这正说明批量写的必要性。
4. **"为什么不用 exFAT？"** → ffconf.h 配了 FF_FS_EXFAT？检查配置再答；一般嵌入式选 FAT32：兼容性最好、实现小、无专利包袱。
5. **"队列元素 512B，队列总占用多少内存？"** → 3×512=1.5KB（从 FreeRTOS 30KB 堆里出），要能算出。
6. **"DMA 缓冲为什么要 4 字节对齐？"** → SDIO FIFO 是 32 位宽，DMA Word 传输要求内存地址 4 对齐；FatFs 给 app 的缓冲一般 512B 自然对齐，但自己 malloc 时要保证。
7. **"写超时设多少？超时了怎么办？"** → SDIO_TRANSFER_TIMEOUT_MS=3000，超时置错误标志、错误计数，返回 SD_ERROR；目前没有自动复位重试（可改进点，主动说）。
