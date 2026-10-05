/**
 ******************************************************************************
 * @file    BMS279.h
 * @brief   GB/T27930-2015电动汽车充电协议解析头文件
 * @version 1.0
 * @date    2025-12-25
 ******************************************************************************
 */

#ifndef __BMS279_H
#define __BMS279_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx.h"
#include <string.h>
#include <stdio.h>

/* CAN ID 定义 */
#define CAN_ID_CHM      0x1826F456  // 充电机握手
#define CAN_ID_BHM      0x182756F4  // 车辆握手
#define CAN_ID_CRM      0x1801F456  // 充电机辨识
#define CAN_ID_CTS      0x1807F456  // 时间同步
#define CAN_ID_CML      0x1808F456  // 充电机最大输出能力
#define CAN_ID_BRO      0x100956F4  // 车辆准备就绪
#define CAN_ID_CRO      0x100AF456  // 充电机准备就绪
#define CAN_ID_BCL      0x181056F4  // 电池充电需求
#define CAN_ID_CCS      0x1812F456  // 充电机充电状态
#define CAN_ID_BSM      0x181356F4  // 电池状态
#define CAN_ID_BMV      0x191556F4  // 单体电池电压
#define CAN_ID_BMT      0x191656F4  // 电池温度
#define CAN_ID_BSP      0x191756F4  // 预留报文
#define CAN_ID_BST      0x101956F4  // 车辆中止充电
#define CAN_ID_CST      0x101AF456  // 充电机中止充电
#define CAN_ID_BSD      0x181C56F4  // 车辆统计数据
#define CAN_ID_CSD      0x181DF456  // 充电机统计数据
#define CAN_ID_BEM      0x081E56F4  // BMS错误报文
#define CAN_ID_CEM      0x081FF456  // 充电机错误报文
#define J1939_CM            0x1CEC56F4        //连接管理参数组号,开启传输
#define J1939_CM_ACK        0x1CECF456        //连接管理参数组号,11确认接收，13接收完成
#define J1939_DT            0x1CEB56F4        //数据传送参数组号,数据传输中


/* 解析结果结构体 */
typedef struct {
    char type_str[16];          // 报文类型字符串
    char description[64];        // 报文描述
    char parsed_info[512];       // 解析出的详细信息
} ParseResult_t;

/* CHM - 充电机握手报文 */
typedef struct {
    uint8_t protocol_version[3]; // 协议版本号
} BMS27930_CHM_t;

/* BHM - 车辆握手报文 */
typedef struct {
    uint16_t max_voltage;        // 最高允许充电总电压 0.1V
} BMS27930_BHM_t;

/* CRM - 充电机辨识报文 */
typedef struct {
    uint8_t result;              // 辨识结果 0=不能辨识 AA=能辨识
    char charger_num[8];         // 充电机编号
} BMS27930_CRM_t;

/* BRM - BMS辨识报文 */
typedef struct {
    uint8_t protocol_version[3]; // 协议版本
    uint8_t battery_type;        // 电池类型
    uint16_t rated_capacity;     // 整车动力蓄电池系统额定容量 0.1Ah
    uint16_t rated_voltage;      // 整车动力蓄电池系统额定总电压 0.1V
    char battery_manufacturer[4]; // 电池生产厂商
    uint32_t battery_pack_num;   // 电池组序号
    uint8_t manufacture_date[3]; // 电池组生产日期
    uint32_t charge_count;       // 电池组充电次数
    uint8_t property_flag;       // 电池组产权标识
    uint8_t reserved;            // 保留字节
    char vin[17];                // 车辆识别码VIN
    uint8_t bms_version[3];      // BMS软件版本号
} BMS27930_BRM_t;

/* BCP - 电池充电参数 */
typedef struct {
    uint16_t max_charge_voltage_single;  // 单体动力蓄电池最高允许充电电压 0.01V
    uint16_t max_charge_current;         // 最高允许充电电流 0.1A (偏移400A)
    uint16_t max_charge_voltage_total;   // 动力蓄电池标称总能量 0.1kWh
    uint16_t max_temperature;            // 最高允许温度
    uint16_t current_soc;                // 整车动力蓄电池荷电状态SOC 1%
    uint16_t current_voltage;            // 整车动力蓄电池当前电压 0.1V
} BMS27930_BCP_t;

/* CTS - 时间同步 */
typedef struct {
    uint8_t year;   // 年
    uint8_t month;  // 月
    uint8_t day;    // 日
    uint8_t hour;   // 时
    uint8_t minute; // 分
    uint8_t second; // 秒
} BMS27930_CTS_t;

/* CML - 充电机最大输出能力 */
typedef struct {
    uint16_t max_output_voltage;  // 最高输出电压 0.1V
    uint16_t min_output_voltage;  // 最低输出电压 0.1V
    uint16_t max_output_current;  // 最大输出电流 0.1A (偏移400A)
    uint16_t min_output_current;  // 最小输出电流 0.1A (偏移400A)
} BMS27930_CML_t;

/* BRO - 车辆准备就绪 */
typedef struct {
    uint8_t ready;  // 0x00=未准备好 0xAA=准备好
} BMS27930_BRO_t;

/* CRO - 充电机准备就绪 */
typedef struct {
    uint8_t ready;  // 0x00=未准备好 0xAA=准备好
} BMS27930_CRO_t;

/* BCL - 电池充电需求 */
typedef struct {
    uint16_t voltage_demand;  // 电压需求 0.1V
    uint16_t current_demand;  // 电流需求 0.1A (偏移400A)
    uint8_t charge_mode;      // 充电模式
} BMS27930_BCL_t;

/* BCS - 电池充电总状态 */
typedef struct {
    uint16_t voltage_measure;  // 充电电压测量值 0.1V
    uint16_t current_measure;  // 充电电流测量值 0.1A (偏移400A)
    uint16_t max_single_voltage; // 最高单体动力蓄电池电压 0.01V
    uint8_t max_single_num;      // 最高单体电池编号
    uint8_t soc;                 // 当前荷电状态SOC 1%
    uint16_t remain_time;        // 估算剩余充电时间 分钟
} BMS27930_BCS_t;

/* CCS - 充电机充电状态 */
typedef struct {
    uint16_t voltage_output;  // 电压输出值 0.1V
    uint16_t current_output;  // 电流输出值 0.1A (偏移400A)
    uint16_t charge_time;     // 累计充电时间 分钟
} BMS27930_CCS_t;

/* BSM - 动力蓄电池状态 */
typedef struct {
    uint8_t max_single_voltage_num;    // 最高单体电池电压所在编号
    uint8_t max_temperature;           // 最高温度 偏移50℃
    uint8_t max_temperature_probe_num; // 最高温度检测点编号
    uint8_t min_temperature;           // 最低温度 偏移50℃
    uint8_t min_temperature_probe_num; // 最低温度检测点编号
    uint8_t voltage_status;            // 单体电池电压过高/过低
    uint8_t soc_status;                // SOC过高/过低
    uint8_t current_status;            // 充电电流过大
    uint8_t temperature_status;        // 温度过高
    uint8_t insulation_status;         // 绝缘状态
    uint8_t connector_status;          // 输出连接器状态
    uint8_t charge_allow_status;       // 允许充电状态
} BMS27930_BSM_t;

/* BST/CST - 中止充电 */
typedef struct {
    uint8_t stop_reason;   // 中止原因
    uint8_t fault_reason;  // 故障原因
    uint8_t error_reason;  // 错误原因
} BMS27930_STOP_t;

/* BSD - 车辆统计数据 */
typedef struct {
    uint8_t soc_before;    // 中止前SOC
    uint8_t soc_after;     // 中止后SOC
    uint16_t min_voltage;  // 最低单体电池电压 0.01V
    uint16_t max_voltage;  // 最高单体电池电压 0.01V
    uint8_t min_temp;      // 最低温度 偏移50℃
    uint8_t max_temp;      // 最高温度 偏移50℃
} BMS27930_BSD_t;

/* CSD - 充电机统计数据 */
typedef struct {
    uint16_t charge_time;  // 累计充电时间 分钟
    uint16_t output_energy; // 输出电能 0.1kWh
} BMS27930_CSD_t;

/* BEM/CEM - 错误报文 */
typedef struct {
    uint8_t recv_timeout;       // 接收超时
    uint8_t recv_format_error;  // 接收报文格式错误
    uint8_t other_error;        // 其他错误
} BMS27930_ERROR_t;

/* 函数声明 */
void BMS27930_ParseMessage(uint32_t canId, uint8_t *data, uint8_t len, ParseResult_t *result);

#ifdef __cplusplus
}
#endif

#endif /* __BMS27930_H */
