#ifndef _RTC_DRIVER_H
#define _RTC_DRIVER_H

#include "stm32f4xx.h"

ErrorStatus RTC_Init_With_LSE(void);
ErrorStatus RTC_SetDateTimeEx(RTC_TimeTypeDef* time, RTC_DateTypeDef* date);
ErrorStatus RTC_GetDateTimeEx(RTC_TimeTypeDef* time, RTC_DateTypeDef* date);
ErrorStatus RTC_SetDateTimeFromString(const char* datetime_str);
void GetCurrentTime(void);
char* GetTimestamp(char* buffer, uint8_t bufferSize);

#endif 
