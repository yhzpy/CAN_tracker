#include "delay.h"
#include "sys.h"
#if SYS_SUPPORT_OS
#include "FreeRTOS.h"						/* FreeRTOS header */
#include "task.h" 							/* task header */
#endif

static u8  fac_us=0;						/* us delay multiplier */
static u16 fac_ms=0;						/* ms delay multiplier, under OS, ticks per ms */

extern void xPortSysTickHandler(void);		/* SysTick handler defined in port.c */

/**
 * @brief       SysTick interrupt service routine
 * @note        forwarded to FreeRTOS
 * @param       none
 * @retval      none
 */
void SysTick_Handler(void)
{
    if(xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}

/**
 * @brief       initialize delay function
 * @note        SysTick clock is AHB clock, usually 168Mhz
 * @param       none
 * @retval      none
 */
void delay_init()
{
	u32 reload;
	SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK);	/* select external clock HCLK */
	fac_us=SystemCoreClock/1000000;						/* fac_us is needed whether OS is used or not */
	reload=SystemCoreClock/1000000;						/* count per second, unit M */
	reload*=1000000/configTICK_RATE_HZ;					/* overflow time set by configTICK_RATE_HZ */
														/* reload is 24-bit, max:16777216, about 0.0998s at 168M */
	fac_ms=1000/configTICK_RATE_HZ;						/* min delay unit of OS */

	SysTick->CTRL|=SysTick_CTRL_TICKINT_Msk;  			/* enable SYSTICK interrupt */
	SysTick->LOAD=reload; 								/* interrupt every 1/configTICK_RATE_HZ sec */
	SysTick->CTRL|=SysTick_CTRL_ENABLE_Msk;   			/* enable SYSTICK */
}

/**
 * @brief       delay n us
 * @note        nus range: 0~798915us (max = 2^24/fac_us @ fac_us=21)
 * @param       nus: us to delay
 * @retval      none
 */
void delay_us(u32 nus)
{
	u32 ticks;
	u32 told,tnow,tcnt=0;
	u32 reload=SysTick->LOAD;				/* LOAD value */
	ticks=nus*fac_us; 						/* ticks needed */
	told=SysTick->VAL;        				/* counter value at entry */
	while(1)
	{
		tnow=SysTick->VAL;
		if(tnow!=told)
		{
			if(tnow<told)tcnt+=told-tnow;	/* note SYSTICK is a down-counter */
			else tcnt+=reload-tnow+told;
			told=tnow;
			if(tcnt>=ticks)break;			/* time over/equal to delay time, exit */
		}
	};
}

/**
 * @brief       delay n ms
 * @note        requires SYS_SUPPORT_OS
 * @param       nms: ms to delay
 * @param       nms: 0~65535
 * @retval      none
 */
void delay_ms(u32 nms)
{
	if(xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED)	/* OS already running */
	{
		if(nms>=fac_ms)						/* delay longer than min OS period */
		{
			vTaskDelay(nms/fac_ms);	 		/* FreeRTOS delay */
		}
		nms%=fac_ms;						/* OS can not provide such small delay, use normal delay */
	}
	delay_us((u32)(nms*1000));				/* normal delay */
}

/**
 * @brief       delay n ms without task scheduling
 * @param       nms: ms to delay
 * @retval      none
 */
void delay_xms(u32 nms)
{
	u32 i;
	for(i=0;i<nms;i++) delay_us(1000);
}
