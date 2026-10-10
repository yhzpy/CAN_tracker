/**
 ******************************************************************************
 * @file    can.h
 * @brief   CAN1/CAN2 驱动头文件（基于 STM32F407 标准库）
 * @note    - CAN1: 中断接收 + FreeRTOS 队列 + 扩展帧
 *          - CAN2: 仅初始化和发送功能 + 扩展帧
 ******************************************************************************
 */

#ifndef __CAN_H
#define __CAN_H

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "queue.h"

/* ==================== 宏定义 ==================== */
#define CAN1_RX_QUEUE_SIZE   10     // CAN1 接收队列深度
/* ==================== CAN1 数据结构 ==================== */

/**
 * @brief  CAN1 接收帧结构体（仅扩展帧）
 */
typedef struct {
    uint32_t extId;     // 29位扩展ID
    uint8_t data[8];    // 数据负载（最多8字节）
    uint8_t dlc;        // 数据长度码（0~8）
} Can1RxFrame;

  /**
   * @brief  CAN日志条目结构体
   */
  typedef struct {
      uint32_t extId;         // 29位扩展ID
      uint8_t data[8];        // 数据负载
      uint8_t dlc;            // 数据长度
      uint32_t timestamp;     // 时间戳（可选，xTaskGetTickCount()）
  } CANLogEntry;

/* ==================== 全局变量声明 ==================== */
extern QueueHandle_t can1_rx_queue;     // CAN1 接收队列句柄

/* ==================== CAN1 函数声明 ==================== */

uint8_t can1_init(uint32_t tsjw, uint32_t tbs2, uint32_t tbs1, uint16_t brp, uint32_t mode);
uint8_t can1_receive_msg(uint32_t *extId, uint8_t *buf, uint32_t timeout_ms);
uint8_t can1_send_msg(uint32_t extId, uint8_t *data, uint8_t len);

/* ==================== CAN2 函数声明 ==================== */

uint8_t can2_init(uint32_t tsjw, uint32_t tbs2, uint32_t tbs1, uint16_t brp, uint32_t mode);
uint8_t can2_send_msg(uint32_t extId, uint8_t *data, uint8_t len);

#endif /* __CAN_H */
