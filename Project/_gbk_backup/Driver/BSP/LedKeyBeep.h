#ifndef __LEDKEYBEEP_H
#define __LEDKEYBEEP_H

#include <stdint.h> 

#define WK_UP     (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_SET)    // 高电平有效
#define KEY_SHORT_PRESS_1   1   // 单击
#define KEY_SHORT_PRESS_2   2   // 双击
#define KEY_LONG_PRESS      3   // 长按

#define TASK_PERIOD_MS      20
#define LONG_PRESS_TICKS    (800  / TASK_PERIOD_MS)   // 800ms = 40个周期
#define DOUBLE_CLICK_TICKS  (300  / TASK_PERIOD_MS)   // 300ms = 15个周期


typedef enum {
		KEY_STATE_IDLE,             // 等待按下
		KEY_STATE_PRESSED,          // 已按下，计时中
		KEY_STATE_RELEASED_ONCE,    // 松开一次，等待是否双击
		KEY_STATE_WAIT_RELEASE,     // 等待松开（长按/双击后防重入）
} KeyState;



void LED_Init(void);
void LED0_ON(void);
void LED0_OFF(void);
void LED0_Turn(void);


void Key_Init(void);
uint8_t Key_Scan(uint8_t mode);



#endif
