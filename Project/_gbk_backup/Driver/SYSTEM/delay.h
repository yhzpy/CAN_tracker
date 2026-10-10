#ifndef __DELAY_H
#define __DELAY_H 			   
#include "stdint.h"


void delay_init(void);
void delay_ms(uint32_t nms);
void delay_us(uint32_t nus);
void delay_xms(uint32_t nms);//延时nms,不会引起任务调度,死等
#endif





























