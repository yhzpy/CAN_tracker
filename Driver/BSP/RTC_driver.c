#include "RTC_driver.h"
#include <stdio.h> 

/**
  * @brief  初始化RTC时钟（使用LSE 32.768kHz晶振）
  * @param  None
  * @retval ErrorStatus: SUCCESS或ERROR
  */
ErrorStatus RTC_Init_With_LSE(void)
{
    // 1. 使能必要的外设时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_BKPSRAM, ENABLE);
    // 2. 允许访问备份域
    PWR_BackupAccessCmd(ENABLE);
//    printf("进入RTC_Init\r\n");
    // 3. 检查是否是首次初始化
    if (RTC_ReadBackupRegister(RTC_BKP_DR1) != 0xA5A5)
    {
        // 4. 复位备份域
        RCC_BackupResetCmd(ENABLE);
        RCC_BackupResetCmd(DISABLE);
        
        // 5. 配置LSE时钟
        RCC_LSEConfig(RCC_LSE_ON);
        
        // 6. 等待LSE就绪（带超时检测）
        uint32_t startupTimeout = 0;
        while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET)
        {
            if (startupTimeout++ > 0xFFFFF) return ERROR;
        }
        
        // 7. 选择RTC时钟源
        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
        
        // 8. 使能RTC时钟
        RCC_RTCCLKCmd(ENABLE);
        
        // 9. 解除写保护
        RTC_WriteProtectionCmd(DISABLE);
        
        // 10. 进入初始化模式
        if (RTC_EnterInitMode() != SUCCESS) return ERROR;
        
        // 11. 配置RTC参数
        RTC_InitTypeDef rtcInit;
        rtcInit.RTC_HourFormat = RTC_HourFormat_24;  // 24小时制
        rtcInit.RTC_AsynchPrediv = 0x7F;             // 异步预分频127
        rtcInit.RTC_SynchPrediv = 0xFF;              // 同步预分频255
        
        if (RTC_Init(&rtcInit) != SUCCESS) return ERROR;
        
        // 12. 退出初始化模式
        RTC_ExitInitMode();
        
        // 13. 等待寄存器同步
        RTC_WaitForSynchro();
        
        // 14. 恢复写保护
        RTC_WriteProtectionCmd(ENABLE);
        
        // 15. 标记已初始化
        RTC_WriteBackupRegister(RTC_BKP_DR1, 0xA5A5);
    }
    return SUCCESS;
}

/**
  * @brief  设置RTC日期和时间（带参数有效性检查）
  * @param  time: 指向RTC_TimeTypeDef结构的指针
  * @param  date: 指向RTC_DateTypeDef结构的指针
  * @retval ErrorStatus: SUCCESS或ERROR
  */
ErrorStatus RTC_SetDateTimeEx(RTC_TimeTypeDef* time, RTC_DateTypeDef* date)
{
    // 参数有效性检查
	
    if (time == NULL || date == NULL) return ERROR;
		if (time->RTC_Hours > 23) return ERROR;   
    if (time->RTC_Minutes > 59) return ERROR;
    if (time->RTC_Seconds > 59) return ERROR;
    // 日期参数检查
    if (date->RTC_WeekDay < 1 || date->RTC_WeekDay > 7) return ERROR;
    if (date->RTC_Month < 1 || date->RTC_Month > 12) return ERROR;
    if (date->RTC_Date < 1 || date->RTC_Date > 31) return ERROR;
    if (date->RTC_Year > 99) return ERROR;
     printf("解除写保护\r\n");
    // 解除写保护
    RTC_WriteProtectionCmd(DISABLE);
     printf("设置时间和日期\r\n");
    // 设置时间和日期
    ErrorStatus timeStatus = RTC_SetTime(RTC_Format_BIN, time);
    ErrorStatus dateStatus = RTC_SetDate(RTC_Format_BIN, date);
    
    // 恢复写保护
    RTC_WriteProtectionCmd(ENABLE);
    
    return (timeStatus == SUCCESS && dateStatus == SUCCESS) ? SUCCESS : ERROR;
}

/**
  * @brief  获取当前RTC日期和时间
  * @param  time: 存储时间的结构体指针
  * @param  date: 存储日期的结构体指针
  * @retval ErrorStatus: SUCCESS或ERROR
  */
ErrorStatus RTC_GetDateTimeEx(RTC_TimeTypeDef* time, RTC_DateTypeDef* date)
{
    if (time == NULL || date == NULL) return ERROR;
    
    // 初始化结构体
    RTC_TimeStructInit(time);
    RTC_DateStructInit(date);
    
    // 获取当前时间和日期
    RTC_GetTime(RTC_Format_BIN, time);
    RTC_GetDate(RTC_Format_BIN, date);
    
    return SUCCESS;
}

/**
 * @brief  根据给定的日期时间字符串设置RTC
 * @param  datetime_str: 日期时间字符串，格式 "YYYY-MM-DD HH:MM:SS"
 * @retval ErrorStatus: SUCCESS 或 ERROR
 */
ErrorStatus RTC_SetDateTimeFromString(const char* datetime_str)
{
    if (datetime_str == NULL || strlen(datetime_str) < 19) {
        printf("Error: Invalid datetime format\r\n");
        return ERROR;
    }
    
    int year, month, day, hour, minute, second;
    
    // 解析字符串 "2026-03-31 14:02:30"
    int parsed = sscanf(datetime_str, "%d-%d-%d %d:%d:%d", 
                       &year, &month, &day, &hour, &minute, &second);
    
    if (parsed != 6) {
        printf("Error: Parse failed, format: YYYY-MM-DD HH:MM:SS\r\n");
        return ERROR;
    }
    
    // 转换为RTC结构
    RTC_TimeTypeDef rtc_time = {0};
    RTC_DateTypeDef rtc_date = {0};
    
    // 设置时间
    rtc_time.RTC_Hours = hour;
    rtc_time.RTC_Minutes = minute;
    rtc_time.RTC_Seconds = second;
    rtc_time.RTC_H12 = (hour >= 12) ? RTC_H12_PM : RTC_H12_AM;  // 自动判断上下午
    
    // 设置日期
    rtc_date.RTC_Year = year-2000;  // RTC年份从2000年开始
    rtc_date.RTC_Month = month;
    rtc_date.RTC_Date = day;
    rtc_date.RTC_WeekDay = 2;//随便设
    
    // 设置RTC - 调用RTC_SetDateTimeEx函数
    ErrorStatus result = RTC_SetDateTimeEx(&rtc_time, &rtc_date);
    
    if (result == SUCCESS) {
        printf("RTC set to: %s\r\n", datetime_str);
    } else {
        printf("Error: RTC_SetDateTimeEx failed\r\n");
    }
    
    return result;
}

//获取当前时间
void GetCurrentTime(void)
{
	RTC_TimeTypeDef currentTime;
	RTC_DateTypeDef currentDate;
	if (RTC_GetDateTimeEx(&currentTime, &currentDate) == SUCCESS)
	{
		// 打印时间
		printf("20%02d-%02d-%02d %02d:%02d:%02d\n",
			   currentDate.RTC_Year,
			   currentDate.RTC_Month,
			   currentDate.RTC_Date,
			   currentTime.RTC_Hours,
			   currentTime.RTC_Minutes,
			   currentTime.RTC_Seconds);
	}
	
}
/**
 * @brief 获取格式化的时间戳字符串 (时.分.秒.毫秒)
 * @param buffer: 用户提供的缓冲区，用于存储生成的时间戳字符串
 * @param bufferSize: 缓冲区的大小。必须足够容纳 "HH.MM.SS.mmm\0" (至少13字节)
 * @return char*: 指向缓冲区的指针，方便直接使用。如果获取失败，返回NULL。
 */
char* GetTimestamp(char* buffer, uint8_t bufferSize) {
    RTC_TimeTypeDef current_time;
    uint32_t sub_seconds = 0;
    uint32_t milliseconds = 0;
    
    // 1. 获取RTC时间和亚秒值
    RTC_GetTime(RTC_Format_BIN, &current_time);
    sub_seconds = RTC_GetSubSecond();
    // 2. 将亚秒转换为毫秒
    // RTC预分频器值(PREDIV_S)
    const uint32_t s_prescaler_s = 255;

      // 计算毫秒
      milliseconds = ((s_prescaler_s - sub_seconds) * 1000) / (s_prescaler_s + 1);
	
	// 3. 格式化输出到缓冲区 (格式: HH:MM:SS.mmm)
	int chars_written = snprintf(buffer, bufferSize, "%02d:%02d:%02d %03d",
                                 current_time.RTC_Hours,
                                 current_time.RTC_Minutes,
                                 current_time.RTC_Seconds,
                                 milliseconds);
    
    // 4. 检查格式化是否成功
    if (chars_written < 0 || (size_t)chars_written >= bufferSize) {
        return NULL; // 格式化失败或缓冲区不足
    }
    
    return buffer;
}

