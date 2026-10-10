/**
 * @file    ymodem_protocol.h
 * @brief   Ymodem协议头文件 - 用于通过串口传输SD卡文件
 * @author  STM32F407 FreeRTOS Project
 * @date    2025
 * @version V2.0
 * @note    修改为使用USART1发送函数和队列接收
 */

#ifndef __YMODEM_PROTOCOL_H
#define __YMODEM_PROTOCOL_H

#include "stm32f4xx.h"

/* Ymodem协议常量定义 */
#define YMODEM_SOH          0x01    /* 128字节包头 */
#define YMODEM_STX          0x02    /* 1024字节包头 */
#define YMODEM_EOT          0x04    /* 传输结束 */
#define YMODEM_ACK          0x06    /* 应答 */
#define YMODEM_NAK          0x15    /* 重传请求 */
#define YMODEM_CAN          0x18    /* 取消传输 */
#define YMODEM_CRC_CHAR     'C'     /* CRC模式请求 */

#define YMODEM_PACKET_SIZE_128   128
#define YMODEM_PACKET_SIZE_1024  1024
#define YMODEM_PACKET_SEQ_MAX    0xFF

/* 命令定义 */
#define CMD_HELP         "help"
#define CMD_LS           "ls"
#define CMD_CD           "cd"
#define CMD_PWD          "pwd"
#define CMD_GET          "get"
#define CMD_EXIT         "exit"
#define CMD_SETTIME      "settime"  

/* 缓冲区大小 */
#define YMODEM_RX_BUF_SIZE    1024
#define YMODEM_FILENAME_LEN   256
#define YMODEM_PATH_LEN       256

/* Ymodem状态 */
typedef enum {
    YMODEM_STATE_IDLE = 0,
    YMODEM_STATE_WAIT_ACK,
    YMODEM_STATE_TRANSFERRING,
    YMODEM_STATE_COMPLETE,
    YMODEM_STATE_CANCEL
} YmodemState;

/* Ymodem上下文 */
typedef struct {
    YmodemState state;
    uint8_t packet_seq;
    uint16_t packet_size;
    uint32_t file_size;
    uint32_t bytes_sent;
    uint8_t retry_count;
    uint8_t use_crc;
} YmodemContext;

/* 文件信息结构 */
typedef struct {
    char name[13];          /* 短文件名(8.3格式) */
    uint32_t size;          /* 文件大小 */
    uint8_t  is_dir;        /* 是否为目录 */
    uint16_t date;          /* 修改日期 */
    uint16_t time;          /* 修改时间 */
} FileInfo;



///* Ymodem接收队列 - 在ymodem_protocol.c中定义 */
//;

/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化Ymodem协议
 */
void Ymodem_Init(void);

/**
 * @brief 处理接收到的命令
 * @param cmd 接收到的命令字符串
 */
void Ymodem_ProcessCommand(char *cmd);

/**
 * @brief 发送文件通过Ymodem协议
 * @param filename 文件名
 * @return 0:成功 -1:失败
 */
int32_t Ymodem_SendFile(const char *filename);

/**
 * @brief 列出当前目录的文件
 */
void Ymodem_ListDirectory(void);

/**
 * @brief 改变当前工作目录
 * @param path 目录路径
 * @return 0:成功 -1:失败
 */
int32_t Ymodem_ChangeDirectory(const char *path);

/**
 * @brief 获取当前工作目录
 */
void Ymodem_PrintCurrentDirectory(void);

/**
 * @brief 显示帮助信息
 */
void Ymodem_PrintHelp(void);

/**
 * @brief Ymodem任务入口
 * @param pvParameters 任务参数
 */
void Ymodem_Task(void *pvParameters);

ErrorStatus RTC_SetDateTimeFromString(const char* datetime_str);

#endif /* __YMODEM_PROTOCOL_H */
