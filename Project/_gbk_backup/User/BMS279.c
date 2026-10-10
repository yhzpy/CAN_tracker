/**
 ******************************************************************************
 * @file    BMS279.c
 * @brief   GB/T27930-2015电动汽车充电协议解析实现文件
 * @version 1.0
 * @date    2025-12-25
 ******************************************************************************
 */

#include "BMS279.h"

/* 私有函数声明 */
static void Parse_CHM(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BHM(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_CRM(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_CTS(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_CML(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BRO(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_CRO(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BCL(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_CCS(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BSM(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BMV(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BMT(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BSP(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BST(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_CST(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BSD(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_CSD(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_BEM(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_CEM(uint8_t *data, uint8_t len, ParseResult_t *result);
static void Parse_1939(uint32_t canID, uint8_t *data, uint8_t len, ParseResult_t *result);

/**
 * @brief  主解析函数 - 根据CAN ID解析报文
 * @param  canId: CAN ID
 * @param  data: 数据指针
 * @param  len: 数据长度
 * @param  result: 解析结果输出指针
 * @retval None
 */
void BMS27930_ParseMessage(uint32_t canId, uint8_t *data, uint8_t len, ParseResult_t *result)
{
    if (result == NULL || data == NULL) {
        return;
    }

    // 清空结果
    memset(result, 0, sizeof(ParseResult_t));

    // 根据CAN ID分发到对应的解析函数
    switch(canId) {
        case CAN_ID_CHM:
            Parse_CHM(data, len, result);
            break;
        case CAN_ID_BHM:
            Parse_BHM(data, len, result);
            break;
        case CAN_ID_CRM:
            Parse_CRM(data, len, result);
            break;
        case CAN_ID_CTS:
            Parse_CTS(data, len, result);
            break;
        case CAN_ID_CML:
            Parse_CML(data, len, result);
            break;
        case CAN_ID_BRO:
            Parse_BRO(data, len, result);
            break;
        case CAN_ID_CRO:
            Parse_CRO(data, len, result);
            break;
        case CAN_ID_BCL:
            Parse_BCL(data, len, result);
            break;
        case CAN_ID_CCS:
            Parse_CCS(data, len, result);
            break;
        case CAN_ID_BSM:
            Parse_BSM(data, len, result);
            break;
        case CAN_ID_BMV:
            Parse_BMV(data, len, result);
            break;
        case CAN_ID_BMT:
            Parse_BMT(data, len, result);
            break;
        case CAN_ID_BSP:
            Parse_BSP(data, len, result);
            break;
        case CAN_ID_BST:
            Parse_BST(data, len, result);
            break;
        case CAN_ID_CST:
            Parse_CST(data, len, result);
            break;
        case CAN_ID_BSD:
            Parse_BSD(data, len, result);
            break;
        case CAN_ID_CSD:
            Parse_CSD(data, len, result);
            break;
        case CAN_ID_BEM:
            Parse_BEM(data, len, result);
            break;
        case CAN_ID_CEM:
            Parse_CEM(data, len, result);
            break;
        default:
						Parse_1939(canId, data, len, result);
            break;
    }
}

/**
 * @brief  解析CHM - 充电机握手报文
 */
static void Parse_CHM(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "CHM");
    sprintf(result->description, "充电机握手");

    if (len >= 3) {
        sprintf(result->parsed_info, "协议版本=%d.%d.%d",
                data[0], data[1], data[2]);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析BHM - 车辆握手报文
 */
static void Parse_BHM(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BHM");
    sprintf(result->description, "车辆握手");

    if (len <= 8) {
        uint16_t max_voltage = (data[1] << 8) | data[0];
        sprintf(result->parsed_info, "最高允许电压=%.1fV", max_voltage * 0.1f);
    } else {
        sprintf(result->parsed_info, "数据长度错误");
    }
}

/**
 * @brief  解析CRM - 充电机辨识报文 
 */
static void Parse_CRM(uint8_t *data, uint8_t len, ParseResult_t *result)
{
		sprintf(result->type_str, "CRM");
		sprintf(result->description, "充电机辨识");

		if (len <= 8) {
				const char *result_str = (data[0] == 0xAA) ? "能辨识" : "不能辨识";

				// 将7字节大端拼接为数值
				uint64_t charger_num = 0;
				for (int i = 0; i < 7; i++) {
						charger_num = (charger_num << 8) | data[1 + i];
				}

				sprintf(result->parsed_info, "辨识结果=%s,充电机编号=%llu",
								result_str, (unsigned long long)charger_num);
		} else {
				sprintf(result->parsed_info, "数据长度错误");
		}
}

/**
 * @brief  解析BCP - 电池充电参数
 */
static void Parse_BCP(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BCP");
    sprintf(result->description, "电池充电参数");

    if (len <= 8) {
        uint16_t max_current = (data[3] << 8) | data[2];
        uint16_t max_voltage = (data[5] << 8) | data[4];
        uint8_t soc = data[6];
        uint16_t current_voltage = (data[1] << 8) | data[0];

        float current_val = (max_current - 400) * 0.1f;

        sprintf(result->parsed_info,
                "最高允许电流=%.1fA,最高允许电压=%.1fV,SOC=%d%%,当前电压=%.1fV",
                current_val, max_voltage * 0.1f, soc, current_voltage * 0.1f);
    } else {
        sprintf(result->parsed_info, "数据长度");
    }
}

/**
 * @brief  解析CTS - 时间同步
 */
static void Parse_CTS(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "CTS");
    sprintf(result->description, "充电机时间同步");

    if (len >= 7) {
        sprintf(result->parsed_info,
                "时间=20%02d-%02d-%02d %02d:%02d:%02d",
                data[0], data[1], data[2], data[3], data[4], data[5]);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析CML - 充电机最大输出能力
 */
static void Parse_CML(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "CML");
    sprintf(result->description, "充电机最大输出能力");

    if (len <= 8) {
        uint16_t max_voltage = (data[1] << 8) | data[0];
        uint16_t min_voltage = (data[3] << 8) | data[2];
        uint16_t max_current = (data[5] << 8) | data[4];
        uint16_t min_current = (data[7] << 8) | data[6];

        sprintf(result->parsed_info,
                "最高电压=%.1fV,最低电压=%.1fV,最大电流=%.1fA,最小电流=%.1fA",
                max_voltage * 0.1f, min_voltage * 0.1f,
                (max_current * 0.1f - 400), (min_current * 0.1f - 400));
    } else {
        sprintf(result->parsed_info, "数据长度");
    }
}

/**
 * @brief  解析BRO - 车辆准备就绪
 */
static void Parse_BRO(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BRO");
    sprintf(result->description, "车辆准备就绪状态");

    if (len >= 1) {
        const char *ready_str = (data[0] == 0xAA) ? "已准备好" : "未准备好";
        sprintf(result->parsed_info, "电池准备状态=%s", ready_str);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析CRO - 充电机准备就绪
 */
static void Parse_CRO(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "CRO");
    sprintf(result->description, "充电机准备就绪状态");

    if (len >= 1) {
        const char *ready_str = (data[0] == 0xAA) ? "已准备好" : "未准备好";
        sprintf(result->parsed_info, "充电机准备状态=%s", ready_str);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析BCL - 电池充电需求
 */
static void Parse_BCL(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BCL");
    sprintf(result->description, "电池充电需求");

    if (len >= 5) {
        uint16_t voltage_demand = (data[1] << 8) | data[0];
        uint16_t current_demand = (data[3] << 8) | data[2];
        uint8_t charge_mode = data[4];

        const char *mode_str = (charge_mode == 0x01) ? "恒压" :
                               (charge_mode == 0x02) ? "恒流" : "未知";

        sprintf(result->parsed_info,
                "电压需求=%.1fV,电流需求=%.1fA,充电模式=%s",
                voltage_demand * 0.1f, 400 - current_demand* 0.1f, mode_str);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}


/**
 * @brief  解析CCS - 充电机充电状态
 */
static void Parse_CCS(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "CCS");
    sprintf(result->description, "充电机充电状态");

    if (len >= 6) {
        uint16_t voltage = (data[1] << 8) | data[0];
        uint16_t current = (data[3] << 8) | data[2];
        uint16_t charge_time = (data[5] << 8) | data[4];

        sprintf(result->parsed_info,
                "电压输出值=%.1fV,电流输出值=%.1fA,累计充电时间=%dmin",
                voltage * 0.1f, 400 - current* 0.1f, charge_time);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析BSM - 动力蓄电池状态
 */
static void Parse_BSM(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BSM");
    sprintf(result->description, "动力蓄电池状态");

    if (len >= 7) {
        uint8_t max_voltage_num = data[0];
        uint8_t max_temp = data[1];
        uint8_t max_temp_num = data[2];
        uint8_t min_temp = data[3];

        int8_t max_temp_val = (int8_t)max_temp - 50;
        int8_t min_temp_val = (int8_t)min_temp - 50;

        sprintf(result->parsed_info,
                "最高温度=%d℃(探测点#%d),最低温度=%d℃,最高电压单体#%d",
                max_temp_val, max_temp_num, min_temp_val, max_voltage_num);

        // 添加异常状态信息
        if (len >= 8 && data[5] != 0) {
            char status_info[64];
            sprintf(status_info, ",异常状态=0x%02X", data[5]);
            strcat(result->parsed_info, status_info);
        }
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析BMV - 单体动力蓄电池电压
 */
static void Parse_BMV(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BMV");
    sprintf(result->description, "单体动力蓄电池电压");

    if (len <= 8) {
        uint8_t start_num = data[0];
        sprintf(result->parsed_info, "起始单体号=%d,电压:", start_num);

        char temp[32];
        for (uint8_t i = 1; i < len && i < 8; i += 2) {
            if (i + 1 < len) {
                uint16_t voltage = (data[i+1] << 8) | data[i];
                sprintf(temp, " %.2fV", voltage * 0.01f);
                strcat(result->parsed_info, temp);
            }
        }
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析BMT - 动力蓄电池温度
 */
static void Parse_BMT(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BMT");
    sprintf(result->description, "动力蓄电池温度");

    if (len >= 2) {
        uint8_t start_num = data[0];
        sprintf(result->parsed_info, "起始探测点号=%d,温度:", start_num);

        char temp[16];
        for (uint8_t i = 1; i < len && i < 8; i++) {
            int8_t temp_val = (int8_t)data[i] - 50;
            sprintf(temp, " %d℃", temp_val);
            strcat(result->parsed_info, temp);
        }
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析BSP - 动力蓄电池预留报文
 */
static void Parse_BSP(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BSP");
    sprintf(result->description, "动力蓄电池预留报文");
    sprintf(result->parsed_info, "预留数据(长度=%d字节)", len);
}

/**
 * @brief  解析BST - 车辆中止充电
 */
static void Parse_BST(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BST");
    sprintf(result->description, "车辆中止充电");

    if (len >= 4) {
        uint8_t stop_reason = data[0];
        uint8_t fault_reason = data[1];
        uint8_t error_reason = data[2];

        const char *stop_str = "";
        switch(stop_reason) {
            case 0x00: stop_str = "正常"; break;
            case 0x01: stop_str = "人工中止"; break;
            case 0x02: stop_str = "故障中止"; break;
            case 0x03: stop_str = "SOC达到需求"; break;
            case 0x04: stop_str = "电压达到需求"; break;
            default: stop_str = "其他"; break;
        }

        sprintf(result->parsed_info,
                "中止原因=%s,故障码=0x%02X,错误码=0x%02X",
                stop_str, fault_reason, error_reason);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析CST - 充电机中止充电
 */
static void Parse_CST(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "CST");
    sprintf(result->description, "充电机中止充电");

    if (len >= 4) {
        uint8_t stop_reason = data[0];
        uint8_t fault_reason = data[1];
        uint8_t error_reason = data[2];

        const char *stop_str = "";
        switch(stop_reason) {
            case 0x00: stop_str = "正常"; break;
            case 0x01: stop_str = "人工中止"; break;
            case 0x02: stop_str = "故障中止"; break;
            case 0x03: stop_str = "电压异常"; break;
            case 0x04: stop_str = "电流异常"; break;
            default: stop_str = "其他"; break;
        }

        sprintf(result->parsed_info,
                "中止原因=%s,故障码=0x%02X,错误码=0x%02X",
                stop_str, fault_reason, error_reason);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析BSD - 车辆统计数据
 */
static void Parse_BSD(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BSD");
    sprintf(result->description, "车辆统计数据");

    if (len >= 8) {
        uint8_t soc_before = data[0];
        uint8_t soc_after = data[1];
        uint16_t min_voltage = (data[3] << 8) | data[2];
        uint16_t max_voltage = (data[5] << 8) | data[4];
        uint8_t min_temp = data[6];
        uint8_t max_temp = data[7];

        sprintf(result->parsed_info,
                "中止前SOC=%d%%,中止后SOC=%d%%,单体电压=%.2f~%.2fV,温度=%d~%d℃",
                soc_before, soc_after,
                min_voltage * 0.01f, max_voltage * 0.01f,
                (int8_t)min_temp - 50, (int8_t)max_temp - 50);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析CSD - 充电机统计数据
 */
static void Parse_CSD(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "CSD");
    sprintf(result->description, "充电机统计数据");

    if (len >= 4) {
        uint16_t charge_time = (data[1] << 8) | data[0];
        uint16_t output_energy = (data[3] << 8) | data[2];

        sprintf(result->parsed_info,
                "累计充电时间=%dmin,输出能量=%.1fkWh",
                charge_time, output_energy * 0.1f);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析BEM - BMS错误报文
 */
static void Parse_BEM(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "BEM");
    sprintf(result->description, "BMS错误报文");

    if (len >= 4) {
        uint8_t recv_timeout = data[0];
        uint8_t recv_format_err = data[1];
        uint8_t other_err = data[2];

        sprintf(result->parsed_info,
                "接收超时=0x%02X,格式错误=0x%02X,其他错误=0x%02X",
                recv_timeout, recv_format_err, other_err);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析CEM - 充电机错误报文
 */
static void Parse_CEM(uint8_t *data, uint8_t len, ParseResult_t *result)
{
    sprintf(result->type_str, "CEM");
    sprintf(result->description, "充电机错误报文");

    if (len >= 4) {
        uint8_t recv_timeout = data[0];
        uint8_t recv_format_err = data[1];
        uint8_t other_err = data[2];

        sprintf(result->parsed_info,
                "接收超时=0x%02X,格式错误=0x%02X,其他错误=0x%02X",
                recv_timeout, recv_format_err, other_err);
    } else {
        sprintf(result->parsed_info, "数据长度不足");
    }
}

/**
 * @brief  解析多包传输
 */
static void Parse_1939(uint32_t canID, uint8_t *data, uint8_t len, ParseResult_t *result)
{
		static uint8_t 	rxbuf[128]={0};//存放组包数据
		static uint8_t   packcnt = 0;  //组包个数
		static uint8_t   pgn = 0;//参数组编号
		static uint8_t   packID = 0;//组包号
		static uint16_t  bytecnt = 0;//组包字节数	
		static uint16_t  pos = 0;

				// 根据PGN类型处理不同报文
		switch(canID)
		{
			case J1939_CM://0x1CEC56F4 连接管理参数组号,开启传输
				if((data[0] & 0xFF) == 0x10)
				{ 
					sprintf(result->type_str, "J1939_CM");
					sprintf(result->description, "车端开启组包传输");
					
					memset(rxbuf, 0, 128);//将 rxbuf 的前 128 个字节全部设置为 0
//					packID = 1;
					bytecnt = (data[1] << 0) | (data[2] << 8);//提取组包字节数
					packcnt = data[3];							//提取组包个数
					pgn = data[6]; 								// 提取组包的PGN	方便后续针对解析
					
					const char* pgn_type = "未知";
					switch(pgn) {
						case 0x02: pgn_type = "BRM"; break;  // 电池请求报文
						case 0x06: pgn_type = "BCP"; break;  // 电池充电参数
						case 0x11: pgn_type = "BCS"; break;  // 电池充电状态
						default:   pgn_type = "其他"; break;
					}
					sprintf(result->parsed_info,"组包字节数=%d, 组包个数=%d, PGN=%s(0x%02X)",bytecnt, packcnt, pgn_type, pgn);
				}
				break;
			case J1939_CM_ACK://0x1CECF456 连接管理参数组号,11确认接收，13接收完成
			{
				if((data[0] & 0xFF) == 0x11)
				{ 
					sprintf(result->type_str, "J1939_CM_ACK");
					sprintf(result->description, "组包确认接收");
					uint8_t packnum = data[1];							//提取组包个数
					
					sprintf(result->parsed_info,"组包个数=%d",packnum);
				}
				else if((data[0] & 0xFF) == 0x13)
				{ 
					sprintf(result->type_str, "J1939_CM_ACK");
					sprintf(result->description, "组包接收完成");
					sprintf(result->parsed_info,"组包接收完成确认");
				}
			}break;
				
			case J1939_DT: //0xEB
			{
				uint8_t receivedPackID = data[0] & 0xFF; //收到DT包编号

				// 有效性检查
				if(receivedPackID == 0 || receivedPackID >packcnt) {
					printf("非法包序号\n");
					return;
				}

				// 静态变量初始化（首次调用时）
				static uint8_t packReceived[7] = {0};

				// 计算存储位置
				uint16_t offset = (receivedPackID-1) * 7;
				uint8_t copyLen = (receivedPackID == packcnt) ? 
								 (bytecnt - offset) : 7;

				// 存储数据
				memcpy(&rxbuf[offset], &data[1], copyLen);
				packReceived[receivedPackID-1] = 1;

				// 检查完成状态
				uint8_t complete = 1;
				for(int i=0; i<packcnt; i++) {
					if(!packReceived[i]) {
						complete = 0;//未完成，需要输出包数
						break;
					}
				}
				sprintf(result->type_str, "J1939_DT");
				sprintf(result->description, "组包发送");
				sprintf(result->parsed_info,"第%d包",receivedPackID);
				
				if(complete) {
					memset(packReceived, 0, sizeof(packReceived));
				//清0接收
				
				// 多包报文消息解析
			   
					switch(pgn)
					{
						case 0x02: // BRM
							sprintf(result->type_str, "BRM");
							sprintf(result->description, "BMS与车辆辨识");
							
							uint8_t protocol_ver[3];//通信协议版本号
							protocol_ver[0] = rxbuf[0];
							protocol_ver[1] = rxbuf[1];
							protocol_ver[2] = rxbuf[2];
							
							uint8_t battery_type = rxbuf[3];//电池类型
							uint16_t rated_cap = (rxbuf[4] | (rxbuf[5] << 8));//整车电池额定容量 0.1Ah
							uint16_t rated_vol = (rxbuf[6] | (rxbuf[7] << 8));//整车电池额定总电压 0.1V
							
							sprintf(result->parsed_info,
									"协议版本=%d.%d.%d, 电池类型=0x%02X, 额定容量=%.1fAh, 额定电压=%.1fV",
									protocol_ver[0], protocol_ver[1], protocol_ver[2] ,battery_type , rated_cap * 0.1f , rated_vol * 0.1f);
							break;
							
						case 0x06: //BCP 动力蓄电池充电参数报文
							sprintf(result->type_str, "BCP");
							sprintf(result->description, "动力蓄电池充电参数");
						
							uint16_t cp_max_vol = (rxbuf[0] | (rxbuf[1] << 8));//单体动力蓄电池最高允许充电电压
							uint16_t max_cur = (rxbuf[2] | (rxbuf[3] << 8));//最高允许充电电流
							
							uint16_t total_cap = (rxbuf[4] | (rxbuf[5] << 8));//动力蓄电池标称总能量     
							uint16_t max_vol = (rxbuf[6] | (rxbuf[7] << 8));//最高允许充电总电压      //0.1位系数  0V偏移量
							
							uint8_t max_tempC = rxbuf[8];//最高允许温度
							
							uint16_t soc = (rxbuf[9] | (rxbuf[10] << 8));//整车动力蓄电池荷电状态   
							uint16_t vol = (rxbuf[11] | (rxbuf[12] << 8));//整车动力蓄电池当前电池电压  
							sprintf(result->parsed_info,"最高允许充电电流=%.1fA, 最高允许充电总电压=%.1fV, 荷电状态=%.1f%%",
									max_cur * 0.1f,max_vol * 0.1f, soc* 0.1f);

							break;
							
						case 0x11:// BCS 电池充电状态报文
							sprintf(result->type_str, "BCS");
							sprintf(result->description, "电池充电状态");
						
							uint16_t vol_measure = rxbuf[0] | (rxbuf[1] << 8); //充电电压测量值
							uint16_t cur_measure = rxbuf[2] | (rxbuf[3] << 8);	//充电电流测量值
							
							uint16_t cp_max_vol1 = rxbuf[4] | ((rxbuf[5] & 0x0F) << 8);//最高单体电池电压						
							soc = rxbuf[6]; //soc 1%
							sprintf(result->parsed_info,"充电电压=%.1fV, 充电电流=%.1fA, SOC=%d%%",vol_measure * 0.1f,cur_measure * 0.1f- 400, soc);

							break;
							
						default:
							break;
					}
				}
				break;
			} 
		}
}
		
