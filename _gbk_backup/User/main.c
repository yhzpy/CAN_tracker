#include <string.h>
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h" 
#include "queue.h" 
#include "timers.h"
#include "semphr.h" 
#include "usart.h"
#include "delay.h"
#include "malloc.h"
#include "LedKeyBeep.h"
#include "OLED.h"
#include "RTC_driver.h"
#include "can.h"
#include "app.h"
#include "BMS279.h"
#include "ff.h"
#include "lcd.h"
#include "RS485.h"
#include "power_mgmt.h"


TaskHandle_t vUART1TaskHandle = NULL;//需要定义在main函数，然后其它文件用extern引用。如果定义在其他地方，会因为初始化顺序不对导致报hard错误
TaskHandle_t vYmodemHandle = NULL;
QueueHandle_t xKeyQueue;//按键队列
QueueHandle_t xSDCardWriteQueue = NULL;//sd队列

QueueHandle_t ymodem_rx_queue = NULL;/* Ymodem接收队列 */
QueueHandle_t lcd_display_queue;		/* lcd接收队列 */
SemaphoreHandle_t xSDCardMutex;  // SD卡互斥量
FATFS *g_fs = NULL;  // 全局文件系统对象


int main()
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4); //中断优先级分组：全部使用抢占式优先级，无子优先级
	delay_init();/*初始化SysTick定时器，每1毫秒产生一次中断*/
	USART1_Init(115200); 
	my_mem_init(SRAMIN);
	RTC_Init_With_LSE();
	OLED_Init();
	Key_Init();
	LED_Init();
	
  adc_vbat_init();// 初始化VBAT ADC
//	Beep_Init();
	lcd_init();                                 /* 初始化LCD */
	lcd_display_dir(1);													/* 设置LCD横屏 */
	RS485_Init(9600);
	can1_init(1, 6, 7, 12, CAN_Mode_Normal);//can1初始化
//	can2_init(1, 6, 7, 12, CAN_Mode_Normal);//can2用于给can1发数据，测试用
	can1_rx_queue = xQueueCreate(CAN1_RX_QUEUE_SIZE, sizeof(Can1RxFrame));//深度10，每个元素大小为 Can1RxFrame
	xKeyQueue = xQueueCreate(10, sizeof(uint8_t));// 创建按键队列（长度10，存储uint8_t类型）
	lcd_display_queue = xQueueCreate(512, sizeof(uint8_t));// 创建lcd队列
	xSDCardWriteQueue = xQueueCreate(3, sizeof(SDCardWriteData_t)); // 创建SD卡写入队列，队列长度为3
	
	lcd_show_string(10, 10, 200, 24, 24, "STM32_CAN_Tracker", RED);
  vTaskCreateFunction();//创建任务
  vTaskStartScheduler();//开启任务调度                
}
	


