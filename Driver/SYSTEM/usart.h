
#ifndef _USART_H
#define _USART_H

#include "stdio.h"
#include "sys.h"
#include "ymodem_protocol.h"

/*******************************************************************************************************/



#define USART_MODE_CAN         0       // 0: Ymodem模式, 1: CAN模式
#define DMA_RX_BUFFER_SIZE     512     // DMA接收缓冲区大小


extern volatile uint8_t idle_flag ;       /* USART接收空闲中断标志 */
extern volatile uint16_t received_len;           // 接收数据长度
extern uint8_t dma_rx_buffer[DMA_RX_BUFFER_SIZE];// DMA接收缓冲区


void USART1_Init(uint32_t baudrate);             /* 串口初始化函数 */
void USART1_received_Poll(void);// 在主循环中调用此函数来处理接收数据，或用中断
void vUART1Task(void *pvParameters);// 串口接收，OS任务

void USART1_SendByte(uint8_t data);   // 发送单字节
void USART1_SendString(char* str);    // 发送字符串

/**
 * @brief 检查接收到的数据是否为Ymodem命令
 * @param data 接收到的数据
 * @param len 数据长度
 * @return 1: 是Ymodem命令, 0: 不是Ymodem命令
 */
uint8_t USART1_IsYmodemCommand(uint8_t *data, uint16_t len);

#endif







