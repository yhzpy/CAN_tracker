/**
 * @file    ymodem_protocol.c
 * @brief   Ymodem协议实现 - 用于通过串口传输SD卡文件
 * @version V2.0
 * @note    修改为使用USART1发送函数和队列接收
 *          SD卡互斥量和FATFS由外部提供
 */

#include "ymodem_protocol.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h" 
#include "semphr.h" 
#include "ff.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "malloc.h"
#include "delay.h"



/* 串口发送函数 - 使用USART1 */
extern void USART1_SendByte(uint8_t data);
extern void USART1_SendString(char* str);
extern SemaphoreHandle_t xSDCardMutex;  // SD卡互斥量
extern FATFS *g_fs;/* 全局文件系统对象 - 在SD卡初始化时已挂载 */
extern QueueHandle_t ymodem_rx_queue;

/* 当前工作目录路径 */
static char current_path[YMODEM_PATH_LEN] = "/";

/* Ymodem上下文 */
static YmodemContext ymodem_ctx;

/* ==================== CRC16计算表 ==================== */
static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

/**
 * @brief 计算CRC16校验
 */
static uint16_t Ymodem_CalcCRC16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0;
    while (len--) {
        crc = (crc << 8) ^ crc16_table[(crc >> 8) ^ *data++];
    }
    return crc;
}

/**
 * @brief 串口发送数据（使用USART1）
 */
static void UART_Send(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        USART1_SendByte(data[i]);
    }
}

/**
 * @brief 串口接收数据（带超时）
 * @param data 接收缓冲区
 * @param len 期望接收长度
 * @param timeout 超时时间（ms）
 * @return 实际接收到的字节数
 */
static uint16_t UART_Receive(uint8_t *data, uint16_t len, uint32_t timeout)
{
    uint16_t count = 0;
    uint32_t tick_start = xTaskGetTickCount();
    uint32_t elapsed;

    while (count < len) {
        /* 从队列获取数据 */
        if (xQueueReceive(ymodem_rx_queue, &data[count], pdMS_TO_TICKS(10)) == pdTRUE) {
            count++;
        }

        elapsed = (xTaskGetTickCount() - tick_start) * portTICK_PERIOD_MS;
        if (elapsed >= timeout) {
            break;
        }
    }

    return count;
}

/**
 * @brief 发送数据包
 */
static int32_t Ymodem_SendPacket(uint8_t *data, uint32_t len, uint8_t seq)
{
    uint8_t header[3];
    uint8_t packet[YMODEM_PACKET_SIZE_1024 + 3];
    uint16_t crc;
    uint32_t total_len;

    /* 选择包头 */
    if (len <= YMODEM_PACKET_SIZE_128) {
        header[0] = YMODEM_SOH;
        total_len = YMODEM_PACKET_SIZE_128;
    } else {
        header[0] = YMODEM_STX;
        total_len = YMODEM_PACKET_SIZE_1024;
    }

    header[1] = seq;
    header[2] = ~seq;

    /* 计算CRC */
    crc = Ymodem_CalcCRC16(data, total_len);

    /* 发送包头 */
    UART_Send(header, 3);

    /* 发送数据 */
    UART_Send(data, total_len);

    /* 发送CRC */
    uint8_t crc_bytes[2] = {(crc >> 8) & 0xFF, crc & 0xFF};
    UART_Send(crc_bytes, 2);

    return 0;
}

/**
 * @brief 等待应答
 */
static int32_t Ymodem_WaitACK(uint32_t timeout_ms)
{
    uint8_t response;
    uint32_t tick_start, elapsed;

    tick_start = xTaskGetTickCount();

    while (1) {
        if (UART_Receive(&response, 1, 100) > 0) {
            if (response == YMODEM_ACK) {
                return 0;
            } else if (response == YMODEM_NAK) {
                return -1;
            } else if (response == YMODEM_CAN) {
                return -2;
            } else if (response == YMODEM_CRC_CHAR) {
                ymodem_ctx.use_crc = 1;
                return 0;
            }
        }

        elapsed = (xTaskGetTickCount() - tick_start) * portTICK_PERIOD_MS;
        if (elapsed >= timeout_ms) {
            return -3;  /* 超时 */
        }

        vTaskDelay(1);
    }
}

/**
 * @brief 发送文件信息包
 */
static int32_t Ymodem_SendFileInfo(const char *filename, uint32_t filesize)
{
    uint8_t info_buf[YMODEM_PACKET_SIZE_128];
    char size_str[32];
    int len;

    memset(info_buf, 0, YMODEM_PACKET_SIZE_128);

    /* 格式: 文件名\0文件大小\0 */
    len = snprintf((char *)info_buf, YMODEM_PACKET_SIZE_128, "%s", filename);
    snprintf(size_str, sizeof(size_str), "%lu", filesize);

    if (len + strlen(size_str) + 2 < YMODEM_PACKET_SIZE_128) {
        strcpy((char *)info_buf + len + 1, size_str);
    }

    /* 发送初始包(seq=0) */
    ymodem_ctx.retry_count = 0;
    do {
        Ymodem_SendPacket(info_buf, YMODEM_PACKET_SIZE_128, 0);

        if (Ymodem_WaitACK(3000) == 0) {
            return 0;
        }

        ymodem_ctx.retry_count++;
        vTaskDelay(50);

    } while (ymodem_ctx.retry_count < 10);

    return -1;
}

/**
 * @brief 发送文件数据
 */
static int32_t Ymodem_SendFileData(FIL *file)
{
		uint8_t *data_buf = (uint8_t *)mymalloc(SRAMIN, YMODEM_PACKET_SIZE_1024);
    // 务必检查内存是否分配成功
    if (data_buf == NULL) {
        return -1; // 内存分配失败，返回错误
    }
    uint32_t bytes_read;
    uint8_t seq = 1;
    uint32_t total_sent = 0;

    ymodem_ctx.packet_seq = 1;
    ymodem_ctx.retry_count = 0;

    while (1) {
        /* 读取文件数据 */
        if (f_read(file, data_buf, YMODEM_PACKET_SIZE_1024, &bytes_read) != FR_OK) {
					  myfree(SRAMIN, data_buf); // 读取失败，释放内存
            return -1;
        }

        /* 如果读取不足1024字节，填充0 */
        if (bytes_read < YMODEM_PACKET_SIZE_1024) {
            memset(data_buf + bytes_read, 0, YMODEM_PACKET_SIZE_1024 - bytes_read);
        }

        /* 发送数据包 */
        ymodem_ctx.retry_count = 0;
        do {
            Ymodem_SendPacket(data_buf,
                bytes_read > YMODEM_PACKET_SIZE_128 ? YMODEM_PACKET_SIZE_1024 : bytes_read,
                seq);

            int32_t ack_result = Ymodem_WaitACK(3000);
            if (ack_result == 0) {
                break;
            } else if (ack_result == -2) {
                /* 对方取消 */
                return -2;
            }

            ymodem_ctx.retry_count++;
            vTaskDelay(50);

        } while (ymodem_ctx.retry_count < 10);

        if (ymodem_ctx.retry_count >= 10) {
            return -1;
        }

        total_sent += bytes_read;
        seq++;

        /* 文件发送完成 */
        if (bytes_read < YMODEM_PACKET_SIZE_1024) {
            break;
        }

        /* 显示进度 */
        if ((seq % 10) == 0) {
//            printf("\rTransferring... %lu bytes", total_sent);
        }
    }
		
		myfree(SRAMIN, data_buf);
    return 0;
}

/**
 * @brief 发送文件
 */
int32_t Ymodem_SendFile(const char *filename)
{
    FIL file;
    uint32_t filesize;
    FRESULT res;
    int32_t ret;

    /* 获取SD卡互斥量 */
    if (xSDCardMutex != NULL) {
        if (xSemaphoreTake(xSDCardMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            printf("\r\nError: Cannot take SD card mutex\r\n");
            return -1;
        }
    }

    /* 打开文件 */
    res = f_open(&file, filename, FA_READ);
    if (res != FR_OK) {
        printf("\r\nError: Cannot open file '%s' (res=%d)\r\n", filename, res);
        if (xSDCardMutex != NULL) {
            xSemaphoreGive(xSDCardMutex);
        }
        return -1;
    }

    filesize = f_size(&file);

    printf("\r\nPreparing to send: %s (%lu bytes)\r\n", filename, filesize);

    /* 等待接收端准备就绪 */
    printf("Waiting for receiver... (Waiting 'C' )\r\n");
		
		 /* 等待接收方发送 'C' 字符 */
		uint8_t start_char;
		uint32_t wait_timeout = 30000;  // 30秒超时
		uint32_t tick_start = xTaskGetTickCount();
		uint32_t elapsed;
		int got_start = 0;

		while (!got_start) {
				if (UART_Receive(&start_char, 1, 1000) > 0) {
						if (start_char == YMODEM_CRC_CHAR) {  // 'C' = 0x43
								ymodem_ctx.use_crc = 1;
								got_start = 1;
								break;
						} else if (start_char == YMODEM_NAK) {  // NAK = 0x15
								ymodem_ctx.use_crc = 0;
								got_start = 1;
								break;
						}
				}

				elapsed = (xTaskGetTickCount() - tick_start) * portTICK_PERIOD_MS;
				if (elapsed >= wait_timeout) {
						printf("\r\nError: Timeout waiting for receiver\r\n");
						f_close(&file);
						return -1;
				}
		}

//		printf("Receiver ready, starting transfer...\r\n");
		

    /* 释放SD卡互斥量 */
    if (xSDCardMutex != NULL) {
        xSemaphoreGive(xSDCardMutex);
    }

    /* 发送文件信息 */
    ret = Ymodem_SendFileInfo(filename, filesize);
    if (ret != 0) {
        printf("\r\nError: Failed to send file info\r\n");
        f_close(&file);
        return -1;
    }

    /* 重新获取SD卡互斥量用于读取数据 */
    if (xSDCardMutex != NULL) {
        if (xSemaphoreTake(xSDCardMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            printf("\r\nError: Cannot take SD card mutex\r\n");
            f_close(&file);
            return -1;
        }
    }

    /* 发送文件数据 */
    ret = Ymodem_SendFileData(&file);

    /* 关闭文件并释放互斥量 */
    f_close(&file);
    if (xSDCardMutex != NULL) {
        xSemaphoreGive(xSDCardMutex);
    }
		
		if (ret == 0) {
          /* 发送EOT */
          uint8_t eot = YMODEM_EOT;
			
					delay_ms(200);//得加这个延时
					UART_Send(&eot, 1);//第一次发送EOT
					Ymodem_WaitACK(1000);//等待ANK
					delay_ms(200);
					UART_Send(&eot, 1);//第2次发送EOT
					if (Ymodem_WaitACK(1000)) { //等待ACK
						if (Ymodem_WaitACK(1000)==0) {//等待C
						delay_ms(200);
						uint8_t null_packet[YMODEM_PACKET_SIZE_128] = {0};
						Ymodem_SendPacket(null_packet,YMODEM_PACKET_SIZE_128,0);
						Ymodem_WaitACK(1000); /*等待最终ACK*/
						//接收并丢弃10个.因为X3he11会发发过来5个0x18,再发5个0x08
						uint8_t dummy;
						for (uint8_t i=0; i<11; i++){
							UART_Receive(&dummy,1,500);
						}
					}
				}
			}
			else if(ret == -2){	
          printf("\r\n\r\nTransfer cancelled by receiver\r\n");
      } else {
          printf("\r\n\r\nTransfer failed\r\n");
      }

      return ret;		
		
}

/**
 * @brief 列出目录内容
 */
void Ymodem_ListDirectory(void)
{
    DIR dir;
    FILINFO fno;
    FRESULT res;
    uint32_t file_count = 0;
    uint32_t dir_count = 0;
    uint32_t total_size = 0;

    /* 获取SD卡互斥量 */
    if (xSDCardMutex != NULL) {
        if (xSemaphoreTake(xSDCardMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            printf("Error: Cannot access SD card\r\n");
            return;
        }
    }

    printf("\r\n");
    printf("Directory of %s\r\n", current_path);
    printf("\r\n");

    /* 打开目录 */
    res = f_opendir(&dir, current_path);
    if (res != FR_OK) {
        printf("Error: Cannot open directory (res=%d)\r\n", res);
        if (xSDCardMutex != NULL) {
            xSemaphoreGive(xSDCardMutex);
        }
        return;
    }

    /* 列表头 */
    printf("%-12s  %10s  %s\r\n", "Name", "Size", "Date");
    printf("------------  ----------  --------\r\n");

    /* 遍历目录 */
    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;

        /* 跳过系统文件 */
        if (fno.fname[0] == '.') continue;

        if (fno.fattrib & AM_DIR) {
            printf("%-12s  %10s  <DIR>\r\n", fno.fname, "");
            dir_count++;
        } else {
            printf("%-12s  %10lu  %04u-%02u-%02u\r\n",
                fno.fname,
                fno.fsize,
                (fno.fdate >> 9) + 1980,
                (fno.fdate >> 5) & 0x0F,
                fno.fdate & 0x1F);
            file_count++;
            total_size += fno.fsize;
        }
    }

    f_closedir(&dir);

    /* 释放SD卡互斥量 */
    if (xSDCardMutex != NULL) {
        xSemaphoreGive(xSDCardMutex);
    }

    printf("------------  ----------  --------\r\n");
    printf("%u file(s), %10lu bytes\r\n", file_count, total_size);
    printf("%u dir(s)\r\n", dir_count);
    printf("\r\n");
}

/**
 * @brief 改变当前目录
 */
int32_t Ymodem_ChangeDirectory(const char *path)
{
    char new_path[YMODEM_PATH_LEN];
    FRESULT res;

    /* 处理相对路径和绝对路径 */
    if (path[0] == '/') {
        strncpy(new_path, path, YMODEM_PATH_LEN);
    } else {
        snprintf(new_path, YMODEM_PATH_LEN, "%s%s", current_path, path);

        /* 确保路径格式正确 */
        uint32_t len = strlen(new_path);
        if (len > 0 && new_path[len-1] != '/') {
            new_path[len] = '/';
            new_path[len+1] = '\0';
        }
    }

    /* 获取SD卡互斥量 */
    if (xSDCardMutex != NULL) {
        if (xSemaphoreTake(xSDCardMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            printf("Error: Cannot access SD card\r\n");
            return -1;
        }
    }

    /* 检查目录是否存在 */
    DIR dir;
    res = f_opendir(&dir, new_path);
    if (res != FR_OK) {
        printf("Error: Path not found: %s\r\n", path);
        if (xSDCardMutex != NULL) {
            xSemaphoreGive(xSDCardMutex);
        }
        return -1;
    }
    f_closedir(&dir);

    /* 释放SD卡互斥量 */
    if (xSDCardMutex != NULL) {
        xSemaphoreGive(xSDCardMutex);
    }

    /* 更新当前路径 */
    strncpy(current_path, new_path, YMODEM_PATH_LEN);

    return 0;
}

/**
 * @brief 打印当前目录
 */
void Ymodem_PrintCurrentDirectory(void)
{
    printf("\r\n%s\r\n", current_path);
}

/**
 * @brief 打印帮助信息
 */
void Ymodem_PrintHelp(void)
{
    printf("\r\n");
    printf("Available Commands:\r\n");
    printf("-------------------\r\n");
    printf("  help          - Show this help message\r\n");
    printf("  ls            - List files in current directory\r\n");
    printf("  cd <dir>      - Change directory (e.g., cd LOGS)\r\n");
    printf("  pwd           - Print working directory\r\n");
    printf("  get <file>    - Download file via Ymodem\r\n");
		printf("  settime <datetime> - Set RTC time (YYYY-MM-DD HH:MM:SS)\r\n"); 
    printf("  exit          - Exit command mode\r\n");
    printf("\r\n");
    printf("Usage:\r\n");
    printf("  In Xshell: Transfer -> Ymodem -> select file to receive\r\n");
    printf("\r\n");
}

/**
 * @brief 处理命令
 */
void Ymodem_ProcessCommand(char *cmd)
{
    char *token;
    char filename[YMODEM_FILENAME_LEN];

    /* 去除空格和换行 */
    while (*cmd == ' ' || *cmd == '\t' || *cmd == '\r' || *cmd == '\n') cmd++;

    if (strlen(cmd) == 0) return;

    /* 解析命令 */
    token = strtok(cmd, " \r\n");

    if (token == NULL) return;

    if (strcmp(token, CMD_HELP) == 0) {
        Ymodem_PrintHelp();
    }
    else if (strcmp(token, CMD_LS) == 0) {
        Ymodem_ListDirectory();
    }
    else if (strcmp(token, CMD_CD) == 0) {
        token = strtok(NULL, " \r\n");
        if (token != NULL) {
            if (strcmp(token, "..") == 0) {
                /* 返回上级目录 */
                uint32_t len = strlen(current_path);
                if (len > 1) {
                    for (int i = len - 2; i >= 0; i--) {
                        if (current_path[i] == '/') {
                            current_path[i + 1] = '\0';
                            break;
                        }
                    }
                }
            } else {
                Ymodem_ChangeDirectory(token);
            }
        } else {
            printf("Usage: cd <directory>\r\n");
        }
    }
    else if (strcmp(token, CMD_PWD) == 0) {
        Ymodem_PrintCurrentDirectory();
    }
    else if (strcmp(token, CMD_GET) == 0) {
        token = strtok(NULL, " \r\n");
        if (token != NULL) {
            /* 构建完整文件路径 */
            char filepath[YMODEM_PATH_LEN];
            snprintf(filepath, YMODEM_PATH_LEN, "%s%s", current_path, token);

            printf("\r\n");
            printf("Initiating Ymodem transfer for: %s\r\n", filepath);
            printf("In Xshell: Transfer -> Receive Ymodem\r\n");
            printf("Then select destination file and click Start\r\n");

            Ymodem_SendFile(filepath);
        } else {
            printf("Usage: get <filename>\r\n");
        }
    }
		 else if (strcmp(token, CMD_SETTIME) == 0) {  // 新增settime命令
        // 获取日期时间字符串
        token = strtok(NULL, "\r\n");  // 获取剩余部分（包含空格）
        if (token != NULL) {
            // 跳过开头的空格
            while (*token == ' ') token++;
            
            if (strlen(token) >= 19) {  // 至少"YYYY-MM-DD HH:MM:SS"长度
                printf("Setting RTC to: %s\r\n", token);
                
                // 调用RTC设置函数
                if (RTC_SetDateTimeFromString(token) == SUCCESS) {
                    printf("RTC time set successfully\r\n");
                } else {
                    printf("Failed to set RTC time\r\n");
                }
            } else {
                printf("Usage: settime YYYY-MM-DD HH:MM:SS\r\n");
                printf("Example: settime 2026-03-31 14:02:30\r\n");
            }
        } else {
            printf("Usage: settime YYYY-MM-DD HH:MM:SS\r\n");
            printf("Example: settime 2026-03-31 14:02:30\r\n");
        }
    }
    else if (strcmp(token, CMD_EXIT) == 0) {
        printf("Exiting command mode. Type 'help' to re-enter.\r\n");
    }
    else {
        printf("Unknown command: %s\r\n", token);
        printf("Type 'help' for available commands.\r\n");
    }
		delay_ms(50);//此处需延时，否则xshe11接收到最后一帧会多出个STM32>而异常关闭。
    printf("STM32> ");
}

/**
 * @brief 初始化Ymodem模块
 */
void Ymodem_Init(void)
{
    /* 创建接收队列 */
    ymodem_rx_queue = xQueueCreate(256, sizeof(uint8_t));
    configASSERT(ymodem_rx_queue != NULL);

    /* 初始化上下文 */
    memset(&ymodem_ctx, 0, sizeof(ymodem_ctx));

    printf("\r\n");
    printf("========================================\r\n");
    printf("  Ymodem File Transfer System Ready\r\n");
    printf("========================================\r\n");
    printf("Type 'help' for available commands\r\n");
    printf("\r\n");
    printf("STM32> ");
}

/**
 * @brief Ymodem任务
 */
void Ymodem_Task(void *pvParameters)
{
    uint8_t rx_char;
    char cmd_buffer[256];
    uint16_t cmd_index = 0;

    Ymodem_Init();

    while (1) {
        /* 从队列获取接收到的字符 */
        if (xQueueReceive(ymodem_rx_queue, &rx_char, portMAX_DELAY) == pdTRUE) {
            /* 回显 */
            USART1_SendByte(rx_char);

            /* 处理命令 */
            if (rx_char == '\r' || rx_char == '\n') {
                printf("\r\n");
                cmd_buffer[cmd_index] = '\0';
                if (cmd_index > 0) {
                    Ymodem_ProcessCommand(cmd_buffer);
                }
                cmd_index = 0;
            } else if (rx_char == '\b' || rx_char == 0x7F) {
                /* 退格键 */
                if (cmd_index > 0) {
                    cmd_index--;
                    printf(" \b");
                }
            } else {
                /* 普通字符 */
                if (cmd_index < sizeof(cmd_buffer) - 1) {
                    cmd_buffer[cmd_index++] = rx_char;
                }
            }
        }
        vTaskDelay(100);
    }
}
