#include "RS485.h"
#include <string.h>

/* 接收缓冲区 */
static uint8_t rs485_rx_buf[RS485_RX_BUF_SIZE];

/* FreeRTOS队列句柄 */
static QueueHandle_t rs485_rx_queue = NULL;

/**
 * @brief  RS485初始化
 * @param  baudrate: 波特率
 * @retval None
 */
void RS485_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* 使能时钟 */
    RCC_AHB1PeriphClockCmd(RS485_GPIO_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(RS485_USART_CLK, ENABLE);
    RCC_AHB1PeriphClockCmd(RS485_DMA_CLK, ENABLE);

    /* GPIO配置 */
    /* TX和RX引脚配置为复用功能 */
    GPIO_InitStructure.GPIO_Pin = RS485_TX_PIN | RS485_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(RS485_GPIO_PORT, &GPIO_InitStructure);

    /* RE引脚配置为输出 */
    GPIO_InitStructure.GPIO_Pin = RS485_RE_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(RS485_GPIO_PORT, &GPIO_InitStructure);

    /* 连接引脚到USART复用功能 */
    GPIO_PinAFConfig(RS485_GPIO_PORT, RS485_TX_PIN_SOURCE, RS485_GPIO_AF);
    GPIO_PinAFConfig(RS485_GPIO_PORT, RS485_RX_PIN_SOURCE, RS485_GPIO_AF);

    /* 默认设置为接收模式 */
    RS485_RX_EN();

    /* USART配置 */
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(RS485_USART, &USART_InitStructure);

    /* 使能USART空闲中断 */
    USART_ITConfig(RS485_USART, USART_IT_IDLE, ENABLE);

    /* 使能USART DMA接收 */
    USART_DMACmd(RS485_USART, USART_DMAReq_Rx, ENABLE);

    /* DMA配置 */
    DMA_DeInit(RS485_DMA_STREAM);
    while (DMA_GetCmdStatus(RS485_DMA_STREAM) != DISABLE);

    DMA_InitStructure.DMA_Channel = RS485_DMA_CHANNEL;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&RS485_USART->DR;
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)rs485_rx_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_BufferSize = RS485_RX_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(RS485_DMA_STREAM, &DMA_InitStructure);

    /* 使能DMA */
    DMA_Cmd(RS485_DMA_STREAM, ENABLE);

    /* NVIC配置 - USART中断 */
    NVIC_InitStructure.NVIC_IRQChannel = RS485_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能USART */
    USART_Cmd(RS485_USART, ENABLE);

    /* 创建FreeRTOS队列 */
    rs485_rx_queue = xQueueCreate(RS485_QUEUE_SIZE, sizeof(RS485_RxPacket_t));
}

/**
 * @brief  RS485发送数据
 * @param  data: 数据指针
 * @param  len: 数据长度
 * @retval None
 */
void RS485_SendData(uint8_t *data, uint16_t len)
{
    uint16_t i;

    /* 切换到发送模式 */
    RS485_TX_EN();

    /* 延时一小段时间，确保RS485芯片切换完成 */
    for(i = 0; i < 100; i++);

    /* 发送数据 */
    for(i = 0; i < len; i++)
    {
        while(USART_GetFlagStatus(RS485_USART, USART_FLAG_TXE) == RESET);
        USART_SendData(RS485_USART, data[i]);
    }

    /* 等待发送完成 */
    while(USART_GetFlagStatus(RS485_USART, USART_FLAG_TC) == RESET);

    /* 延时一小段时间 */
    for(i = 0; i < 100; i++);

    /* 切换回接收模式 */
    RS485_RX_EN();
}

/**
 * @brief  RS485发送单字节
 * @param  byte: 要发送的字节
 * @retval None
 */
void RS485_SendByte(uint8_t byte)
{
    RS485_SendData(&byte, 1);
}

/**
 * @brief  获取RS485接收队列句柄
 * @retval 队列句柄
 */
QueueHandle_t RS485_GetRxQueue(void)
{
    return rs485_rx_queue;
}

/**
 * @brief  USART6中断服务函数
 * @retval None
 */
void RS485_USART_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    RS485_RxPacket_t rx_packet;
    uint16_t rx_len;

    if(USART_GetITStatus(RS485_USART, USART_IT_IDLE) != RESET)
    {
        /* 清除空闲中断标志 */
        volatile uint32_t tmp = RS485_USART->SR;
        tmp = RS485_USART->DR;

        /* 停止DMA */
        DMA_Cmd(RS485_DMA_STREAM, DISABLE);
			/* 清除DMA标志位 - 关键！*/
          DMA_ClearFlag(DMA2_Stream1, DMA_FLAG_TCIF1 | DMA_FLAG_HTIF1 |
                        DMA_FLAG_TEIF1 | DMA_FLAG_DMEIF1 | DMA_FLAG_FEIF1);

        /* 计算接收到的数据长度 */
        rx_len = RS485_RX_BUF_SIZE - DMA_GetCurrDataCounter(RS485_DMA_STREAM);

        if(rx_len > 0 && rx_len <= RS485_RX_BUF_SIZE)
        {
            /* 复制数据到数据包结构 */
            memcpy(rx_packet.data, rs485_rx_buf, rx_len);
            rx_packet.len = rx_len;

            /* 将数据包发送到队列 */
            xQueueSendFromISR(rs485_rx_queue, &rx_packet, &xHigherPriorityTaskWoken);
        }

        /* 重新配置DMA */
        DMA_SetCurrDataCounter(RS485_DMA_STREAM, RS485_RX_BUF_SIZE);
        DMA_Cmd(RS485_DMA_STREAM, ENABLE);

        /* 如果需要，进行任务切换 */
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
