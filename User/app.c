#include <stdio.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h" 
#include "semphr.h" 
#include "LedKeyBeep.h"
#include "OLED.h"
#include "usart.h"
#include "app.h"
#include "sdio_sdcard.h"
#include "malloc.h"
#include "can.h"
#include "RTC_driver.h"
#include "BMS279.h"
#include "ymodem_protocol.h"
#include "ff.h"/* FATFS头文件 */
#include "lcd.h"
#include "RS485.h"
#include "power_mgmt.h"



uint16_t time_ms=500;//LED指示灯翻转间隔
extern QueueHandle_t xKeyQueue;//按键队列
extern TaskHandle_t vUART1TaskHandle;//串口任务句柄
extern TaskHandle_t vYmodemHandle;//Ymodem任务句柄
extern QueueHandle_t xSDCardWriteQueue;//SD写队列
extern SemaphoreHandle_t xSDCardMutex;  // SD卡互斥量
extern FATFS *g_fs;  // 文件系统对象
extern QueueHandle_t lcd_display_queue;//lcd接收队列

// SD写入函数
  static void write_to_sd(FIL *fil, const char *buffer, uint16_t len) {
      RTC_TimeTypeDef time;
      RTC_DateTypeDef date;
      char filename[32];
      FRESULT res;
      UINT bw;

      // 获取当前日期生成文件名
      if (RTC_GetDateTimeEx(&time, &date) == SUCCESS) {
          snprintf(filename, sizeof(filename), "0:/BMS_log_%04d%02d%02d.csv",
                   date.RTC_Year + 2000, date.RTC_Month, date.RTC_Date);
      } else {
          strcpy(filename, "0:/BMS_log_default.csv");
      }

      if (xSemaphoreTake(xSDCardMutex, portMAX_DELAY) == pdTRUE) {
          res = f_open(fil, filename, FA_OPEN_APPEND | FA_WRITE);

          if (res == FR_NO_FILE) {
              res = f_open(fil, filename, FA_CREATE_NEW | FA_WRITE);
          }

          if (res == FR_OK) {
              res = f_write(fil, buffer, len, &bw);

              if (res != FR_OK || bw != len) {
                  printf("Error: Failed to write to SD card, error code: %d\r\n", res);
              }

              f_close(fil);
          } else {
              printf("Error: Failed to open %s, error code: %d\r\n", filename, res);
          }

          xSemaphoreGive(xSDCardMutex);
      }
  }

//打印任务剩余栈空间
void vTaskMonitor(void *pvParameters) 
	{
    for (;;) {
        // 打印任务列表头
        printf("TaskName\tStatus\tPriority\tStackRemain\tCore\n");
        
        // 分配缓冲区并打印任务列表
        char pcBuffer[512];  // 建议缓冲区≥512字节以容纳多任务信息
        vTaskList(pcBuffer);
        printf("%s\n", pcBuffer);  // 打印任务列表
			
				UBaseType_t uxHighWater = uxTaskGetStackHighWaterMark(vUART1TaskHandle);
				printf("vUART1TaskHandle min_stack:%lu KB\n", uxHighWater * sizeof(uint32_t));
        
        // 每5秒执行一次（使用FreeRTOS延时，非阻塞）
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

//按键检测
void vKeyScanTask(void *pvParameters) {
      KeyState state = KEY_STATE_IDLE;
      uint16_t tick_count = 0;
      uint8_t  key_value;

      for (;;) {
          switch (state) {

              case KEY_STATE_IDLE:
                  if (WK_UP == 1) {
                      state = KEY_STATE_PRESSED;
                      tick_count = 0;
                  }
                  break;

              case KEY_STATE_PRESSED:
                  if (WK_UP == 1) {
                      tick_count++;
                      if (tick_count >= LONG_PRESS_TICKS) {
                          // 长按阈值到达，立即触发
                          key_value = KEY_LONG_PRESS;
                          xQueueSend(xKeyQueue, &key_value, 0);
                          state = KEY_STATE_WAIT_RELEASE;
                      }
                  } else {
                      // 松开且未到长按阈值，等待是否双击
                      state = KEY_STATE_RELEASED_ONCE;
                      tick_count = 0;
                  }
                  break;

              case KEY_STATE_RELEASED_ONCE:
                  if (WK_UP == 1) {
                      // 300ms内再次按下 → 双击
                      key_value = KEY_SHORT_PRESS_2;
                      xQueueSend(xKeyQueue, &key_value, 0);
                      state = KEY_STATE_WAIT_RELEASE;
                  } else {
                      tick_count++;
                      if (tick_count >= DOUBLE_CLICK_TICKS) {
                          // 超时未再按 → 单击
                          key_value = KEY_SHORT_PRESS_1;
                          xQueueSend(xKeyQueue, &key_value, 0);
                          state = KEY_STATE_IDLE;
                      }
                  }
                  break;

              case KEY_STATE_WAIT_RELEASE:
                  // 等待松开，防止长按/双击后立即触发下一次
                  if (WK_UP == 0) {
                      state = KEY_STATE_IDLE;
                  }
                  break;
          }

          vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
      }
  }

//定时器回调函数，LED闪烁,电池电量检测
void vLEDTimerCallback(TimerHandle_t xTimer) {
     static uint8_t low_battery_flag = 0;  // 防止重复进入休眠
      uint16_t vbat_voltage;

      LED0_Turn();  // 切换LED状态

      // 读取电池电压
      vbat_voltage = adc_read_vbat();

      // 可选：打印电压值用于调试
//       printf("VBAT: %d mV\n", vbat_voltage);

      // 检查电压是否过低
      if (vbat_voltage < VBAT_LOW_THRESHOLD && !low_battery_flag) {
          low_battery_flag = 1;

          printf("Low battery detected: %d mV, entering low power mode...\n", vbat_voltage);

          // 延时让串口发送完成
          vTaskDelay(pdMS_TO_TICKS(100));

          // 进入低功耗模式
          enter_low_power_mode();
      }
}

//创建LED闪烁 软件定时器
void vLED1Task(void *pvParameters) {
    uint16_t delay_ms = *(uint16_t *)pvParameters;  // 获取延时参数（如500ms）
    
    // 创建周期定时器（自动重载）
    TimerHandle_t xLedTimer = xTimerCreate(//创建不会自动跑 必须手动start 见195行
        "LEDTimer",                     // 定时器名称（调试用）
        pdMS_TO_TICKS(delay_ms),        // 延时时间转换为Tick数
        pdTRUE,                         // 自动重载（周期模式）  无限跑
        (void *)delay_ms,               // 传递参数（可选）
        vLEDTimerCallback               // 回调函数
    );

    if (xLedTimer != NULL) {
        xTimerStart(xLedTimer, 0);      // 启动定时器（非阻塞）
    }
    vTaskDelete(NULL);  // 删除当前任务（由定时器接管LED控制）
}

// BMS27930记录任务
void BMS27930_RecordTask(void *pvParameters)
{
	 static uint32_t last_extId = 0;
	 static uint8_t last_data[8] = {0};
	 static uint8_t last_len = 0;

	 uint32_t extId = 0;
	 uint8_t buf[8] = {0};
	 uint8_t len = 0;
	 char timestamp[16];
	 ParseResult_t parse_result;
	 SDCardWriteData_t sd_data;

	 // 任务主循环
	 while (1) {
			 // 接收CAN报文，超时时间100ms
			 len = can1_receive_msg(&extId, buf, 100);		 

			 if (len > 0) {		 
				 
					 // 获取时间戳
					 GetTimestamp(timestamp, sizeof(timestamp));

					 // 判断是否与上一帧相同
					 uint8_t is_same = (extId == last_extId) && (len == last_len);
					 if (is_same) {
							 for (uint8_t i = 0; i < len; i++) {
									 if (buf[i] != last_data[i]) {
											 is_same = 0;
											 break;
									 }
							 }
					 }

					 if (is_same) {
							 // 相同报文，只记录原始数据
							 sprintf(sd_data.log_buffer, "%s,%08X,", timestamp, (unsigned int)extId);
							 for (uint8_t i = 0; i < len; i++) {
									 sprintf(sd_data.log_buffer + strlen(sd_data.log_buffer), "%02X%s", buf[i], (i < len - 1) ? " " :"");
								}
							 strcat(sd_data.log_buffer, "\r\n");
							 }
						else {
							 // 不同报文，解析并记录详细信息
							 BMS27930_ParseMessage(extId, buf, len, &parse_result);

							 // 格式化输出
							 sprintf(sd_data.log_buffer, "%s,%08X,", timestamp, (unsigned int)extId);
							 for (uint8_t i = 0; i < len; i++) {
									 sprintf(sd_data.log_buffer + strlen(sd_data.log_buffer), "%02X%s", buf[i], (i < len - 1) ? " " :"");
							 }
							 sprintf(sd_data.log_buffer + strlen(sd_data.log_buffer), ",%s,%s,%s\r\n",
											 parse_result.type_str,
											 parse_result.description,
											 parse_result.parsed_info);
								
							 // 保存当前报文
							 last_extId = extId;
							 last_len = len;
							 memcpy(last_data, buf, len);
							
							 }
							  // 使用队列通知SD卡任务写入
							 if (xSDCardWriteQueue != NULL) {
									 if (xQueueSend(xSDCardWriteQueue, &sd_data, pdMS_TO_TICKS(10)) != pdPASS) {
											 // 队列满，发送失败
											 printf("Warning: SD card write queue full\r\n");
									 }
							//写入LCD显示队列
              if (lcd_display_queue != NULL) {
								
									// 查询队列剩余空间
									UBaseType_t spaces = uxQueueSpacesAvailable(lcd_display_queue);
									UBaseType_t waiting = uxQueueMessagesWaiting(lcd_display_queue);

									// 如果队列使用率超过80%，打印警告
									if (spaces < 100) {  // 512*0.2 ≈ 100
											printf("LCD queue nearly full: %u/%u used\r\n",
														 (unsigned int)waiting,
														 (unsigned int)(waiting + spaces));
									}
								
                  char lcd_buf[128];

                  if (is_same) {
                      // 相同报文
                      strcpy(lcd_buf, sd_data.log_buffer);
                  } else {
                      // 不同报文：只显示到type_str
                      sprintf(lcd_buf, "%s,%08X,", timestamp, (unsigned int)extId);
                      for (uint8_t i = 0; i < len; i++) {
                          sprintf(lcd_buf + strlen(lcd_buf), "%02X%s", buf[i], (i < len - 1) ? " " : "");
                      }
                      sprintf(lcd_buf + strlen(lcd_buf), ",%s\r\n", parse_result.type_str);
                  }

									uint16_t slen = (uint16_t)strlen(lcd_buf);
									for (uint16_t j = 0; j < slen; j++) {
											uint8_t ch = (uint8_t)lcd_buf[j];   // 先存入变量
											xQueueSend(lcd_display_queue, &ch, 0); // 再取地址
									}
              }

//							 printf("%s", sd_data.log_buffer);//写入sd的内容，同时输出到串口

					 }
			 }
	 }
}

// SD卡写入任务
//缓存一个扇区（512字节）或者1min无新增数据时，写入一次。避免频繁写入写坏SD卡。
void SDCard_WriteTask(void *pvParameters) {
		SDCardWriteData_t sd_data;
		// 分配FIL结构体
        /*f_open 时 FatFs 会在卡上找到（或新建）这个文件的目录项，把起始簇等布局信息填进 FIL；
        之后 f_write 就靠 FIL 里记录的 fptr/clust/sect 决定"下一次该往哪个簇哪个扇区写"，
        写完推进这些字段——这就是为什么 write_to_sd 每次 open/write/close都是操作同一个 fil 指针，
        它相当于文件在你 RAM 里的"状态卡"。*/
		FIL *fil = (FIL *)mymalloc(SRAMIN, sizeof(FIL));

		if (fil == NULL) {
				printf("Error: Failed to allocate memory for FIL\r\n");
				vTaskDelete(NULL);
				return;
		}

		char accumulate_buffer[512];//第二个缓冲区，用于累积数据
		uint16_t buffer_pos = 0;
		TickType_t last_receive_time = xTaskGetTickCount();

		while (1) {
				//在指定的超时时间内等待队列数据，但如果距离上次接收已经超时，就立即处理当前缓冲区中的数据
				TickType_t elapsed = xTaskGetTickCount() - last_receive_time;
				TickType_t wait_time = (elapsed >= pdMS_TO_TICKS(WRITE_TIMEOUT_MS))
															 ? 0
															 : pdMS_TO_TICKS(WRITE_TIMEOUT_MS) - elapsed;

				if (xQueueReceive(xSDCardWriteQueue, &sd_data, wait_time) == pdPASS) {
						// 收到新数据
						uint16_t data_len = strlen(sd_data.log_buffer);

						// 如果加入新数据会超出缓冲区，先写入当前数据
						if (buffer_pos + data_len > SD_SECTOR_SIZE && buffer_pos > 0) {
								write_to_sd(fil, accumulate_buffer, buffer_pos);
								buffer_pos = 0;
						}

						// 累积数据
						memcpy(accumulate_buffer + buffer_pos, sd_data.log_buffer, data_len);
						buffer_pos += data_len;
						last_receive_time = xTaskGetTickCount();

						// 缓冲区满 512 字节，写入
						if (buffer_pos >= SD_SECTOR_SIZE) {
								write_to_sd(fil, accumulate_buffer, buffer_pos);
								buffer_pos = 0;
						}

				} else {
						// 超时：1分钟无新数据，写入缓冲区
						if (buffer_pos > 0) {
								write_to_sd(fil, accumulate_buffer, buffer_pos);
								buffer_pos = 0;
						}
						last_receive_time = xTaskGetTickCount();
				}
		}
		myfree(SRAMIN, fil);
}

// 按键处理任务
//按0打印栈使用情况
void vKeyProcessTask(void *pvParameters)
  {
      uint8_t key_value;

      while (1) {
          if (xQueueReceive(xKeyQueue, &key_value, portMAX_DELAY) == pdPASS) {

              switch (key_value) {

                  case KEY_SHORT_PRESS_1:
                      // 短按1次：点亮屏幕
//                      lcd_display_on();
											lcd_backlight_on();

                      break;

                  case KEY_SHORT_PRESS_2:
                      // 短按2次：打印栈使用情况
                      printf("TaskName\tStatus\tPriority\tStackRemain\tCore\n");

                      char pcBuffer[512];
                      vTaskList(pcBuffer);
                      printf("%s\n", pcBuffer);

                      UBaseType_t uxHighWater = uxTaskGetStackHighWaterMark(vUART1TaskHandle);
                      printf("vUART1TaskHandle min_stack:%lu KB\n", uxHighWater * sizeof(uint32_t));

                      uxHighWater = uxTaskGetStackHighWaterMark(vYmodemHandle);
                      printf("vYmodemHandle min_stack:%lu KB\n", uxHighWater * sizeof(uint32_t));
                      break;

                  case KEY_LONG_PRESS:
                      // 长按：关闭屏幕
//                      lcd_display_off();
											lcd_backlight_off();
                      break;

                  default:
                      break;
              }
          }
      }
  }

//sd卡初始化，创建互斥量
void SD_Card_Init(void *pvParameters)
  {
      FRESULT ret;
      static FATFS *fs_obj = NULL;   // 使用静态变量，保持文件系统对象在整个运行期间有效

      /* 任务初始化延迟，等待SD卡初始化完成 */
      vTaskDelay(pdMS_TO_TICKS(400));

      /* 创建SD卡互斥量 */
      xSDCardMutex = xSemaphoreCreateMutex();
      if (xSDCardMutex == NULL) {
          printf("[ERROR] Failed to create SD card mutex\r\n");
          vTaskDelete(NULL);
          return;
      }

      /* 分配文件系统和文件对象内存 */
      fs_obj = mymalloc(SRAMIN, sizeof(FATFS));
      if (fs_obj == NULL) {
          printf("[ERROR] FATFS内存分配失败！\r\n");
          vTaskDelete(NULL);  /* 删除自身任务 */
          return;
      }

      /* 挂载SD卡（驱动器0）*/
      /*"0:" 就是"把 fs_obj 挂到这台 MCU 唯一的那个卷上"，它经由 diskio.c 的 DEV_SD = 0 映射到 SD 卡驱动。*/
      /*结构体里确实没有值——但这没关系，因为 f_mount 的工作就是"往里面填值"*/
      /*结构体初始是空台账，f_mount 负责"注册 + 从 SD 卡抄数据填满它"；
      而"这是 SD 卡"这件事由卷号 0 → diskio.c 的 DEV_SD 映射决定，与结构体内容无关。*/
      ret = f_mount(fs_obj, "0:", 1);
      if (ret) {
          printf("[WARNING] mount fail %d\r\n", ret);
          vTaskDelete(NULL);  /* 删除自身任务 */
          return;
      }
      printf("SD mount success\r\n");

      /* 任务执行完成，删除自身 */
      vTaskDelete(NULL);
  }
	
//LCD显示
//从队列中读取到buffer,100ms执行一次LCD刷新。如果数据太快可能丢失，可增大缓冲区
  void vLCDDisplayTask(void *pvParameters) {
          uint8_t  buffer[512];  // 缓冲区，存储100ms内的数据
          uint16_t buffer_pos = 0;

          const uint8_t LINE_HEIGHT    = 16;
          const uint8_t MAX_LINES      = 14;
          const uint8_t CHARS_PER_LINE = 40;

          static char  line_cache[14][41];
          static uint8_t filled = 0;

          TickType_t last_process_tick = xTaskGetTickCount();

          while(1) {
                  // 持续读取队列到buffer（只是简单的字节拷贝，很快），读到数据或者buffer满跳出循环
                  while(buffer_pos < sizeof(buffer) &&
                        xQueueReceive(lcd_display_queue, &buffer[buffer_pos], 0) == pdPASS) {
                          buffer_pos++;
                  }

                  // 100ms到期才执行解析+刷屏
                  TickType_t now = xTaskGetTickCount();
                  if((now - last_process_tick >= pdMS_TO_TICKS(100)) && buffer_pos > 0) {
                          // 解析buffer中的数据
                          uint16_t i = 0;
                          while(i < buffer_pos) {
                                  char line_buf[41];
                                  uint8_t line_len = 0;

                                  while(i < buffer_pos && line_len < CHARS_PER_LINE) {
                                          uint8_t ch = buffer[i++];
                                          if(ch == '\n' || ch == '\r') break;
                                          if(ch >= ' ' && ch <= '~') {
                                                  line_buf[line_len++] = (char)ch;
                                          } else if(line_len <= CHARS_PER_LINE - 4) {
                                                  line_buf[line_len++] = '<';
                                                  line_buf[line_len++] = (ch >> 4) > 9 ? 'A'+(ch>>4)-10 : '0'+(ch>>4);
                                                  line_buf[line_len++] = (ch & 0x0F) > 9 ? 'A'+(ch&0x0F)-10 :'0'+(ch&0x0F);
                                                  line_buf[line_len++] = '>';
                                          }
                                  }
                                  line_buf[line_len] = '\0';
                                  if(line_len == 0) continue;

                                  if(filled < MAX_LINES) {
                                          strncpy(line_cache[filled], line_buf, 41);
                                          filled++;
                                  } else {
                                          for(uint8_t r = 0; r < MAX_LINES - 1; r++) {
                                                  strncpy(line_cache[r], line_cache[r+1], 41);
                                          }
                                          strncpy(line_cache[MAX_LINES - 1], line_buf, 41);
                                  }
                          }

                          // 刷新LCD
                          for(uint8_t r = 0; r < filled; r++) {
                                  lcd_fill(0, r * LINE_HEIGHT, 320, (r + 1) * LINE_HEIGHT, WHITE);
                                  lcd_show_string(0, r * LINE_HEIGHT, 320, LINE_HEIGHT, 16, line_cache[r], BLACK);
                          }

                          buffer_pos = 0;  // 清空buffer
                          last_process_tick = now;
                  }

                  vTaskDelay(pdMS_TO_TICKS(5));
          }
  }
/*******************************************************************************
* Function Name  : vTaskCreateFunction(void)
* Description    : 系统所有任务创建函数
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void vTaskCreateFunction(void)
{      // 128是栈深度 单位是字    time_ms是传给任务函数的参数  然后是优先级  最后是任务句柄的输出指针
        xTaskCreate( vLED1Task, 		 "vLED1Task",  		128, 		&time_ms, 	1,	0		);	//LED工作任务 优先级1
		xTaskCreate( vUART1Task, 		 "vUART1Task",		128,		NULL, 		4, 	&vUART1TaskHandle);	//串口接收
		xTaskCreate( vKeyScanTask, 		 "vKeyScanTask", 	128, 		NULL, 		2, 	NULL);	//按键检测
		xTaskCreate( vKeyProcessTask, 	 "vKeyProcess", 	256, 		NULL, 		2, 	NULL);	//按键任务
//		xTaskCreate( vTaskMonitor, 		 "vTaskMonitor", 	512, 		NULL, 		2, 	NULL);	//任务监控
		xTaskCreate( SD_Card_Init,     	 "SD_Card_Init",    256,        NULL,       2,  NULL);//SDCard_Write
		xTaskCreate( BMS27930_RecordTask,"BMSRecord", 		512,        NULL,       3,  NULL);//BMSRecord
		xTaskCreate( SDCard_WriteTask,   "SDCard_Write",    512,        NULL,       2,  NULL);//SDCard_Write（栈512）
		xTaskCreate( Ymodem_Task,     	 "Ymodem_Task",     512,        NULL,       3,  &vYmodemHandle);//Ymodem_Task交互协议
		xTaskCreate( vLCDDisplayTask,    "vLCDDisplayTask", 512,        NULL,       2,  NULL);//lcd显示任务
}





