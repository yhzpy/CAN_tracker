#include "stm32f4xx.h"                  // Device header
#include "LedKeyBeep.h"
#include "delay.h"

/******************************LED部分******************************/
void LED_Init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //9--led0--红色
	GPIO_InitStructure.GPIO_Speed = GPIO_Fast_Speed;		// 设置输出速度为 50MHz
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;		// 设置为输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;     // 设置输出类型为推挽
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;   // 设置不上下拉
	GPIO_Init(GPIOF, &GPIO_InitStructure);
	
	GPIO_SetBits(GPIOF, GPIO_Pin_9);
}

void LED0_ON(void)
{
	GPIO_ResetBits(GPIOF, GPIO_Pin_9);
}

void LED0_OFF(void)
{
	GPIO_SetBits(GPIOF, GPIO_Pin_9);
}

void LED0_Turn(void)
{
	if (GPIO_ReadOutputDataBit(GPIOF, GPIO_Pin_9) == 0)
	{
		GPIO_SetBits(GPIOF, GPIO_Pin_9);
	}
	else
	{
		GPIO_ResetBits(GPIOF, GPIO_Pin_9);
	}
}


/******************************Key按键部分******************************/


/**
  * @brief  按键GPIO初始化
  * @note   
	*         KEY_UP(PA0): 高电平有效，内部下拉 
  */
void Key_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    /* 1. 使能GPIO时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	
    /* 3. 配置KEY_UP (PA0) */
		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;          // PA0
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;       // 输入模式
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;     // 内部下拉
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz; // 输入速度
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    GPIO_Init(GPIOA, &GPIO_InitStructure);
}





