#ifndef _POWER_MGMT_H
#define _POWER_MGMT_H

#include "stm32f4xx.h"

#define VBAT_LOW_THRESHOLD  3200  // 低电压阈值 3.2V (单位mV)

void adc_vbat_init(void);
uint16_t adc_read_vbat(void);
void enter_low_power_mode(void);

#endif 
