/**
 ******************************************************************************
 * @file    can.c
 * @brief   CAN1/CAN2 驱动实现（基于 STM32F407 标准库 + FreeRTOS）
 * @note    - CAN1: PA11(RX) / PA12(TX)，中断接收模式
 *          - CAN2: PB5(RX) / PB6(TX)，仅发送模式
 *          - 仅支持 29位扩展帧
 ******************************************************************************
 */

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "can.h"

/* ==================== 全局变量 ==================== */
QueueHandle_t can1_rx_queue = NULL;         // CAN1 接收队列句柄，can1的中断生产数据，can1_receive_msg函数消费数据
static uint32_t can1_rx_lost_count = 0;    // CAN1 丢包计数器（队列满导致）

/* ====================================================================================
 *                                   CAN1 相关实现
 * ====================================================================================
 */

/**
 * @brief       CAN1 硬件初始化（中断接收模式）
 * @param       tsjw    : 重新同步跳跃时间单元，范围: 1~3
 * @param       tbs2    : 时间段2的时间单元，范围: 1~8
 * @param       tbs1    : 时间段1的时间单元，范围: 1~16
 * @param       brp     : 波特率分频器，范围: 1~1024
 * @note        以上4个参数在函数内部会减1，所以任何参数都不能为0
 *              CAN1挂在APB1上，输入时钟 = 42MHz
 *              波特率 = 42M / ((tsjw + tbs1 + tbs2) * brp)
 *              例如: can1_init(1, 6, 7, 6, CAN_Mode_Normal)
 *              波特率 = 42M / ((1 + 6 + 7) * 6) = 500Kbps
 * @param       mode    : CAN_Mode_Normal（普通模式）或 CAN_Mode_LoopBack（回环模式）
 * @retval      0-成功，1-失败
 */
uint8_t can1_init(uint32_t tsjw, uint32_t tbs2, uint32_t tbs1, uint16_t brp, uint32_t mode)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    CAN_InitTypeDef CAN_InitStructure;
    CAN_FilterInitTypeDef CAN_FilterInitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* 1. 使能时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);    // GPIOA 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);     // CAN1 时钟

    /* 2. 配置 GPIO（PA11=RX, PA12=TX） */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource11, GPIO_AF_CAN1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource12, GPIO_AF_CAN1);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;             // 复用功能
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;           // 推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;             // 上拉
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 3. 配置 CAN1 控制器参数 */
    CAN_DeInit(CAN1);
    CAN_StructInit(&CAN_InitStructure);

    CAN_InitStructure.CAN_TTCM = DISABLE;        // 非时间触发模式
    CAN_InitStructure.CAN_ABOM = DISABLE;        // 手动离线管理
    CAN_InitStructure.CAN_AWUM = DISABLE;        // 手动唤醒
    CAN_InitStructure.CAN_NART = DISABLE;        // 使能自动重传
    CAN_InitStructure.CAN_RFLM = DISABLE;        // 报文不锁定（新报文覆盖旧报文）
    CAN_InitStructure.CAN_TXFP = DISABLE;        // 发送优先级由ID决定
    CAN_InitStructure.CAN_Mode = mode;           // 工作模式 CAN_Mode_Silent可以配置成静默接收
    CAN_InitStructure.CAN_SJW = tsjw - 1;        // 重新同步跳跃宽度
    CAN_InitStructure.CAN_BS1 = tbs1 - 1;        // 时间段1
    CAN_InitStructure.CAN_BS2 = tbs2 - 1;        // 时间段2
    CAN_InitStructure.CAN_Prescaler = brp;       // 波特率分频器

    if (CAN_Init(CAN1, &CAN_InitStructure) != CAN_InitStatus_Success)
    {
        return 1;   // 初始化失败
    }

    /* 4. 配置过滤器（接收所有扩展帧） */
    CAN_FilterInitStructure.CAN_FilterNumber = 0;                       // 过滤器0（CAN1使用0~13）
    CAN_FilterInitStructure.CAN_FilterMode = CAN_FilterMode_IdMask;     // 掩码模式 模糊匹配    ps ：：idList 列表模式 精确匹配
    CAN_FilterInitStructure.CAN_FilterScale = CAN_FilterScale_32bit;    // 32位
    CAN_FilterInitStructure.CAN_FilterIdHigh = 0x0000;                  // 不过滤    一切报文全部放行
    CAN_FilterInitStructure.CAN_FilterIdLow = 0x0000;
    CAN_FilterInitStructure.CAN_FilterMaskIdHigh = 0x0000;
    CAN_FilterInitStructure.CAN_FilterMaskIdLow = 0x0000;
    CAN_FilterInitStructure.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;// 关联到FIFO0
    CAN_FilterInitStructure.CAN_FilterActivation = ENABLE;              // 激活过滤器
    CAN_FilterInit(&CAN_FilterInitStructure);

    /* 5. 配置 NVIC 中断（优先级必须 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY） */
    NVIC_InitStructure.NVIC_IRQChannel = CAN1_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;  // 抢占优先级5（FreeRTOS要求）
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;         // 子优先级0
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 6. 使能 CAN1 FIFO0 接收中断 */
    CAN_ITConfig(CAN1, CAN_IT_FMP0, ENABLE);

    return 0;   // 初始化成功
}


/**
 * @brief  从 CAN1 队列接收报文
 * @param  extId      : 输出参数，接收到的29位扩展ID
 * @param  buf        : 数据缓冲区（至少8字节）
 * @param  timeout_ms : 阻塞超时时间（毫秒），0表示立即返回
 * @retval 0-超时/队列空，其他值-接收到的DLC（1~8）
 */
uint8_t can1_receive_msg(uint32_t *extId, uint8_t *buf, uint32_t timeout_ms)
{
    Can1RxFrame rxFrame;

    // 参数检查
    if (extId == NULL || buf == NULL)
    {
        return 0;
    }

    // 从队列接收（支持阻塞等待）
    if (xQueueReceive(can1_rx_queue, &rxFrame, pdMS_TO_TICKS(timeout_ms)) == pdPASS)
    {
        *extId = rxFrame.extId;

        //复制数据到用户缓冲区
        for (uint8_t i = 0; i < rxFrame.dlc && i < 8; i++)
        {
            buf[i] = rxFrame.data[i];
        }

        return rxFrame.dlc;  // 返回实际数据长度
    }

    return 0;  // 超时或队列为空
}

/**
 * @brief  CAN1 发送报文（仅扩展帧）
 * @param  extId : 29位扩展ID
 * @param  data  : 待发送数据
 * @param  len   : 数据长度（1~8）
 * @retval 0-成功，1-参数错误，2-发送失败
 */
uint8_t can1_send_msg(uint32_t extId, uint8_t *data, uint8_t len)
{
    CanTxMsg TxMessage;

    // 参数检查
    if (len > 8 || len == 0 || data == NULL)
    {
        return 1;
    }

    // 配置发送消息（固定扩展帧）
    TxMessage.ExtId = extId;                // 29位扩展ID
    TxMessage.IDE = CAN_Id_Extended;        // 扩展帧标志
    TxMessage.RTR = CAN_RTR_Data;           // 数据帧（非远程帧）
    TxMessage.DLC = len;                    // 数据长度

    // 填充数据
    for (uint8_t i = 0; i < len; i++)
    {
        TxMessage.Data[i] = data[i];
    }

    // 发送报文（选择空闲邮箱）
    if (CAN_Transmit(CAN1, &TxMessage) == CAN_TxStatus_NoMailBox)
    {
        return 2;  // 发送失败（无可用邮箱）
    }

    return 0;  // 发送成功
}

/**
 * @brief  CAN1 FIFO0 接收中断服务函数
 * @note   当 CAN1 FIFO0 接收到新报文时自动触发
 *         本函数在中断上下文执行，必须尽快退出
 */
void CAN1_RX0_IRQHandler(void)/*名字在中断向量表 中断发生时 cpu根据名字找地址*/
{
    CanRxMsg RxMessage;                             // 标准库接收消息结构体
    Can1RxFrame rxFrame;                            // 自定义帧结构
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;  // 任务唤醒标志

    //检查是否为 FIFO0 接收中断  FIFO0 是 CAN 硬件外设（bxCAN）内部的接收缓冲队列，不是 FreeRTOS 队列，是芯片里的硬件结构。
    //控制器有 2 个接收 FIFO（FIFO0 / FIFO1），每个 3 级深；
//每一级能存一帧完整报文（ID + DLC + 最多 8 字节数据）；
//报文通过过滤器匹配后，按过滤器配置存入指定 FIFO。
/*控制器有 2 个接收 FIFO（FIFO0 / FIFO1），每个 3 级深；
每一级能存一帧完整报文（ID + DLC + 最多 8 字节数据）；
报文通过过滤器匹配后，按过滤器配置存入指定 FIFO。*/
/*这个项目的接收链路是硬件 FIFO（3 级）→ 软件队列（深度 10）两级缓冲：
硬件层保证突发不丢帧，软件层把数据从"中断上下文"安全传递给"任务上下文"*/
    if (CAN_GetITStatus(CAN1, CAN_IT_FMP0) != RESET)
    {
        //从 FIFO0 读取报文（自动释放 FIFO 空间）
        CAN_Receive(CAN1, CAN_FIFO0, &RxMessage);

        // 提取扩展ID（只处理扩展帧，忽略标准帧）
        rxFrame.extId = RxMessage.ExtId;

        // 提取数据长度码（0~8）
        rxFrame.dlc = RxMessage.DLC;

        // 复制有效数据
        // 双重保护：1) i < DLC 只复制有效字节  2) i < 8 防止数组越界
        for (uint8_t i = 0; i < RxMessage.DLC && i < 8; i++)
        {
            rxFrame.data[i] = RxMessage.Data[i];
        }

        // 将报文发送到 FreeRTOS 队列
        // 使用 FromISR 版本（不阻塞），如果队列满则丢弃
        /*xHigherPriorityTaskWoken  假如因为发送帧而解除了高优先级任务在这里的阻塞 则系统会自动给这个标志位置true 这样中断结束后立刻切换到高优先级任务
        ps::   被唤醒的任务优先级 > 当前被中断（正在运行）的任务优先级  这样才有必要立刻切换去那个任务*/
        if (xQueueSendFromISR(can1_rx_queue, &rxFrame, &xHigherPriorityTaskWoken) != pdPASS)
        {
            can1_rx_lost_count++;  // 记录丢包（队列满）
        }

        // 清除中断挂起标志（必须清除，否则会重复进入）
        CAN_ClearITPendingBit(CAN1, CAN_IT_FMP0);

        // 如果有更高优先级任务被唤醒，请求任务切换
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/* ====================================================================================
 *                                   CAN2 相关实现
 * ====================================================================================
 */

/**
 * @brief       CAN2 硬件初始化（仅发送模式，无中断）
 * @param       tsjw    : 重新同步跳跃时间单元，范围: 1~3
 * @param       tbs2    : 时间段2的时间单元，范围: 1~8
 * @param       tbs1    : 时间段1的时间单元，范围: 1~16
 * @param       brp     : 波特率分频器，范围: 1~1024
 * @note        以上4个参数在函数内部会减1，所以任何参数都不能为0
 *              CAN2挂在APB1上，输入时钟 = 42MHz
 *              波特率 = 42M / ((tsjw + tbs1 + tbs2) * brp)
 *              例如: can2_init(1, 6, 7, 6, CAN_Mode_Normal)
 *              波特率 = 42M / ((1 + 6 + 7) * 6) = 500Kbps
 * @param       mode    : CAN_Mode_Normal（普通模式）或 CAN_Mode_LoopBack（回环模式）
 * @retval      0-成功，1-失败
 */
uint8_t can2_init(uint32_t tsjw, uint32_t tbs2, uint32_t tbs1, uint16_t brp, uint32_t mode)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    CAN_InitTypeDef CAN_InitStructure;
    CAN_FilterInitTypeDef CAN_FilterInitStructure;

    /* 1. 使能时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);    // GPIOB 时钟
//    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);     // CAN1 时钟（CAN2依赖CAN1 ?）
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN2, ENABLE);     // CAN2 时钟

    /* 2. 配置 GPIO（PB5=RX, PB6=TX） */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource5, GPIO_AF_CAN2);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_CAN2);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;             // 复用功能
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;           // 推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;             // 上拉
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 3. 配置 CAN2 控制器参数 */
    CAN_DeInit(CAN2);
    CAN_StructInit(&CAN_InitStructure);

    CAN_InitStructure.CAN_TTCM = DISABLE;        // 非时间触发模式
    CAN_InitStructure.CAN_ABOM = DISABLE;        // 手动离线管理
    CAN_InitStructure.CAN_AWUM = DISABLE;        // 手动唤醒
    CAN_InitStructure.CAN_NART = DISABLE;        // 使能自动重传
    CAN_InitStructure.CAN_RFLM = DISABLE;        // 报文不锁定
    CAN_InitStructure.CAN_TXFP = DISABLE;        // 发送优先级由ID决定
    CAN_InitStructure.CAN_Mode = mode;           // 工作模式
    CAN_InitStructure.CAN_SJW = tsjw - 1;        // 重新同步跳跃宽度
    CAN_InitStructure.CAN_BS1 = tbs1 - 1;        // 时间段1
    CAN_InitStructure.CAN_BS2 = tbs2 - 1;        // 时间段2
    CAN_InitStructure.CAN_Prescaler = brp;       // 波特率分频器

    if (CAN_Init(CAN2, &CAN_InitStructure) != CAN_InitStatus_Success)
    {
        return 1;   // 初始化失败
    }

    /* 4. 配置过滤器（CAN2 使用过滤器 14~27） */
    CAN_FilterInitStructure.CAN_FilterNumber = 14;                      // 过滤器14
    CAN_FilterInitStructure.CAN_FilterMode = CAN_FilterMode_IdMask;     // 掩码模式
    CAN_FilterInitStructure.CAN_FilterScale = CAN_FilterScale_32bit;    // 32位
    CAN_FilterInitStructure.CAN_FilterIdHigh = 0x0000;                  // 不过滤
    CAN_FilterInitStructure.CAN_FilterIdLow = 0x0000;
    CAN_FilterInitStructure.CAN_FilterMaskIdHigh = 0x0000;
    CAN_FilterInitStructure.CAN_FilterMaskIdLow = 0x0000;
    CAN_FilterInitStructure.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;// 关联到FIFO0
    CAN_FilterInitStructure.CAN_FilterActivation = ENABLE;              // 激活过滤器
    CAN_FilterInit(&CAN_FilterInitStructure);

    /* 注意：CAN2 不配置中断，仅用于发送 */

    return 0;   // 初始化成功
}

/**
 * @brief  CAN2 发送报文（仅扩展帧）
 * @param  extId : 29位扩展ID
 * @param  data  : 待发送数据
 * @param  len   : 数据长度（1~8）
 * @retval 0-成功，1-参数错误，2-发送失败
 */
uint8_t can2_send_msg(uint32_t extId, uint8_t *data, uint8_t len)
{
    CanTxMsg TxMessage;

    // 参数检查
    if (len > 8 || len == 0 || data == NULL)
    {
        return 1;
    }

    // 配置发送消息（固定扩展帧）
    TxMessage.ExtId = extId;                // 29位扩展ID
    TxMessage.IDE = CAN_Id_Extended;        // 扩展帧标志
    TxMessage.RTR = CAN_RTR_Data;           // 数据帧
    TxMessage.DLC = len;                    // 数据长度

    // 填充数据
    for (uint8_t i = 0; i < len; i++)
    {
        TxMessage.Data[i] = data[i];
    }

    // 发送报文
    if (CAN_Transmit(CAN2, &TxMessage) == CAN_TxStatus_NoMailBox)
    {
        return 2;  // 发送失败（无可用邮箱）
    }

    return 0;  // 发送成功
}
