/**
 ****************************************************************************************************
 * @file        usart.c
 * @author      WYW
 * @version     V1.1
 * @date        2025-08-12
 * @brief       串口初始化和接收处理(使用串口1)，支持printf
 * @license
 ****************************************************************************************************
 * @attention
 * stm32f407使用串口1+DMA和串口空闲中断实现接收。支持printf
 * 修改内容：
 * - 添加Ymodem命令检测功能
 * - 如果接收到Ymodem命令，则发送到Ymodem队列
 * - 如果接收到其他数据，则通过CAN2发送
 ****************************************************************************************************
 */

#include "sys.h"
#include "usart.h"
#include <string.h>
#include <ctype.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* 接收缓冲区 */
uint8_t dma_rx_buffer[DMA_RX_BUFFER_SIZE];

/* Ymodem接收队列 - 在ymodem_protocol.c中定义 */
extern QueueHandle_t ymodem_rx_queue;
extern QueueHandle_t lcd_display_queue;//lcd接收队列

// 全局变量
volatile uint8_t idle_flag = 0;                     // 空闲中断标志
volatile uint16_t received_len = 0;                 // 接收数据长度
extern TaskHandle_t vUART1TaskHandle;


/******************************************************************************************/
/* 以下代码, 支持printf函数，需要选择use MicroLIB */

#if 1
#if (__ARMCC_VERSION >= 6010050)                    /* 使用AC6编译器时 */
__asm(".global __use_no_semihosting\n\t");          /* 禁止半主机模式 */
__asm(".global __ARM_use_no_argv \n\t");            /* AC6编译需要定义main函数为无参数形式，否则部分函数可能出现半主机模式 */

#else
/* 使用AC5编译器时, 需要在这里定义__FILE 才能使用半主机模式 */
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
    /* Whatever you require here. If the only file you are using is */
    /* standard output using printf() for debugging, no file handling */
    /* is required. */
};

#endif

/* 不使用半主机模式，至少需要重写以下函数_ttywrch\_sys_exit\_sys_command_string，同时兼容AC6和AC5模式 */
int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

/* 定义_sys_exit()以避免使用半主机模式 */
void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}

/* FILE 在 stdio.h中规定. */
FILE __stdout;

/* 重定向fputc函数, printf的输出将通过fputc函数输出到串口 */
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);               /* 等待上一个字符发送完成 */

    USART1->DR = (uint8_t)ch;                       /* 将要发送的字符 ch 写入到DR寄存器 */
    return ch;
}
#endif
/***********************************************END*******************************************/


/**
 * @brief   USART1初始化函数
 * @param   baudrate: 波特率
 * @note
 * @retval  无
 */
void USART1_Init(uint32_t baudrate) {
    GPIO_InitTypeDef GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;
    DMA_InitTypeDef DMA_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;

    // 1. 使能时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);     // GPIOA时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);    // USART1时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);      // DMA2时钟

    // 2. 配置GPIO引脚功能：PA9-TX, PA10-RX
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;        // 复用模式
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;      // 推挽输出
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;        // 上拉
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 3. 映射引脚到该功能
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);  // PA9映射为USART1_TX
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1); // PA10映射为USART1_RX

    // 4. 配置USART1参数
    USART_InitStruct.USART_BaudRate = baudrate;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStruct);

    // 5. 配置DMA接收（DMA2 Stream5, Channel4）
    DMA_DeInit(DMA2_Stream5);
    DMA_InitStruct.DMA_Channel = DMA_Channel_4;
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
    DMA_InitStruct.DMA_Memory0BaseAddr = (uint32_t)dma_rx_buffer;
    DMA_InitStruct.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStruct.DMA_BufferSize = DMA_RX_BUFFER_SIZE;
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;//内存地址递增
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Circular;     // 循环模式
    DMA_InitStruct.DMA_Priority = DMA_Priority_High;
    DMA_InitStruct.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
    DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA2_Stream5, &DMA_InitStruct);

    // 6. 使能USART1 DMA接收
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
    DMA_Cmd(DMA2_Stream5, ENABLE);

    // 7. 配置空闲中断
    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);
    NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    // 8. 使能USART1
    USART_Cmd(USART1, ENABLE);
}
 /**
 * @brief 将ASCII十六进制字符转换为数值
 * @param c ASCII字符 ('0'-'9', 'A'-'F', 'a'-'f')
 * @return 对应的数值 (0-15)，无效字符返回0xFF
 */
static uint8_t hex_char_to_value(char c)
{
		if (c >= '0' && c <= '9') {
				return c - '0';
		} else if (c >= 'A' && c <= 'F') {
				return c - 'A' + 10;
		} else if (c >= 'a' && c <= 'f') {
				return c - 'a' + 10;
		}
		return 0xFF;  // 无效字符
}

//串口空闲中断服务函数
void USART1_IRQHandler(void)
{
	  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
		{
			volatile uint32_t tmp = USART1->SR;  // 步骤1：读取SR寄存器
        tmp = USART1->DR;                   // 步骤2：读取DR寄存器，清除空闲中断标志

         // 停止DMA传输，计算接收长度
        DMA_Cmd(DMA2_Stream5, DISABLE);
        received_len = DMA_RX_BUFFER_SIZE - DMA_GetCurrDataCounter(DMA2_Stream5);// 获取接收长度

		 // 重启DMA传输，设置计数器并使能，
        DMA_SetCurrDataCounter(DMA2_Stream5, DMA_RX_BUFFER_SIZE);
        DMA_Cmd(DMA2_Stream5, ENABLE);

			vTaskNotifyGiveFromISR(vUART1TaskHandle, &xHigherPriorityTaskWoken);///*释放任务通知-ISR版本 */

			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);///*如果需要的话强制进行上下文切换 */
    }
}

/**
 * @brief 检查接收到的数据是否为Ymodem命令
 * @param data 接收到的数据
 * @param len 数据长度
 * @return 1: 是Ymodem命令, 0: 不是Ymodem命令
 */
uint8_t USART1_IsYmodemCommand(uint8_t *data, uint16_t len)
{
		char cmd_str[32];
		uint16_t cmd_len = 0;

		if (len == 0 || data == NULL) {
				return 0;
		}

		// 直接提取命令字符串（遇到空格或换行结束）
		for (uint16_t i = 0; i < len && i < DMA_RX_BUFFER_SIZE; i++) {
				if (data[i] == ' ' || data[i] == '\r' || data[i] == '\n' || data[i] == '\t') {
						break;
				}
				if (cmd_len < sizeof(cmd_str) - 1) {
						cmd_str[cmd_len++] = tolower((int)data[i]);
				}
		}
		cmd_str[cmd_len] = '\0';

		// 判断是否为Ymodem命令
		if (strcmp(cmd_str, CMD_HELP) == 0 ||
				strcmp(cmd_str, CMD_LS) == 0 ||
				strcmp(cmd_str, CMD_CD) == 0 ||
				strcmp(cmd_str, CMD_PWD) == 0 ||
				strcmp(cmd_str, CMD_GET) == 0 ||
				strcmp(cmd_str, CMD_EXIT) == 0) {
				return 1;
		}

		return 0;
}

/**
 * @brief 将接收到的数据发送到Ymodem队列
 * @param data 接收到的数据
 * @param len 数据长度
 */
static void USART1_SendToYmodemQueue(uint8_t *data, uint16_t len)
{
		if (ymodem_rx_queue == NULL) {
				return;
		}

		// 将每个字符发送到Ymodem队列
		for (uint16_t i = 0; i < len && i < DMA_RX_BUFFER_SIZE; i++) {
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xQueueSend(ymodem_rx_queue, &data[i], 0);
				// 同时发送到LCD显示队列
				if (lcd_display_queue != NULL) {
            xQueueSend(lcd_display_queue, &data[i], 0);
        }
		}
}

 /* UART1 处理任务 */
void vUART1Task(void *pvParameters)
{
			uint32_t ulNotificationValue;
			while(1)
			{
							/* 等待中断服务函数发送的通知 */
							ulNotificationValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

							if(ulNotificationValue > 0)
							{
											/* Ymodem模式：所有数据发送到Ymodem队列 */
											USART1_SendToYmodemQueue(dma_rx_buffer, received_len);
							}
			}
}

// 发送单字节
void USART1_SendByte(uint8_t data)
{
    // 等待发送缓冲区为空
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    // 发送数据
    USART_SendData(USART1, data);
}

// 发送字符串
void USART1_SendString(char* str)
{
    while (*str)
    {
        USART1_SendByte(*str++);
    }
}
