#ifndef __RS485_H
#define __RS485_H

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "queue.h"

/* RS485引脚定义 */
#define RS485_USART                 USART6
#define RS485_USART_CLK             RCC_APB2Periph_USART6
#define RS485_USART_IRQn            USART6_IRQn
#define RS485_USART_IRQHandler      USART6_IRQHandler

#define RS485_GPIO_CLK              RCC_AHB1Periph_GPIOG
#define RS485_GPIO_PORT             GPIOG
#define RS485_TX_PIN                GPIO_Pin_14
#define RS485_RX_PIN                GPIO_Pin_9
#define RS485_RE_PIN                GPIO_Pin_10

#define RS485_TX_PIN_SOURCE         GPIO_PinSource14
#define RS485_RX_PIN_SOURCE         GPIO_PinSource9
#define RS485_GPIO_AF               GPIO_AF_USART6

/* DMA配置 */
#define RS485_DMA_CLK               RCC_AHB1Periph_DMA2
#define RS485_DMA_STREAM            DMA2_Stream1
#define RS485_DMA_CHANNEL           DMA_Channel_5
#define RS485_DMA_IRQn              DMA2_Stream1_IRQn
#define RS485_DMA_IRQHandler        DMA2_Stream1_IRQHandler

/* RS485控制宏 */
#define RS485_TX_EN()               GPIO_SetBits(RS485_GPIO_PORT, RS485_RE_PIN)
#define RS485_RX_EN()               GPIO_ResetBits(RS485_GPIO_PORT, RS485_RE_PIN)

/* 接收缓冲区大小 */
#define RS485_RX_BUF_SIZE           128
#define RS485_QUEUE_SIZE            5

/* RS485数据包结构 */
typedef struct {
    uint8_t data[RS485_RX_BUF_SIZE];
    uint16_t len;
} RS485_RxPacket_t;

/* 函数声明 */
void RS485_Init(uint32_t baudrate);
void RS485_SendData(uint8_t *data, uint16_t len);
void RS485_SendByte(uint8_t byte);
QueueHandle_t RS485_GetRxQueue(void);

#endif /* __RS485_H */
