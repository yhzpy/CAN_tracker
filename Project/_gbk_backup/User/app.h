#ifndef __APP_H
#define __APP_H

#define SD_SECTOR_SIZE 512
#define WRITE_TIMEOUT_MS 60000  // 1分钟

// SD卡写入队列数据结构
 typedef struct {
     char log_buffer[512];	//临时存储单个队列项
 } SDCardWriteData_t;

void vTaskCreateFunction(void);
#endif

