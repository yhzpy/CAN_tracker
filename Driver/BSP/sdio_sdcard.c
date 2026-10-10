#include "string.h"
#include "usart.h"
#include "sdio_sdcard.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"


/* 仅在本.c文件使用的全局变量 */
static uint8_t CardType = STD_CAPACITY_SD_CARD_V1_1; /* 当前识别到的卡类型 */
static uint32_t CSD_Tab[4], CID_Tab[4], RCA = 0;		/* CSD/CID 原始数据 & 相对地址 */

static SemaphoreHandle_t g_sdio_transfer_sem = NULL;  // 传输完成信号量
static SemaphoreHandle_t g_sdio_mutex = NULL;         // SD卡访问互斥锁

/* 传输超时时间(ms) */
#define SDIO_TRANSFER_TIMEOUT_MS    3000

/* 传输状态标志 - 用于中断同步 */
static volatile uint8_t g_sdio_transfer_end = 0;      // SDIO传输结束标志
static volatile uint8_t g_sdio_transfer_error = 0;    // 传输错误标志
static volatile SD_Error g_sdio_error = SD_OK;        // 具体错误状态

/* 当前传输块数（用于中断中判断是否需要发送停止命令） */
static volatile uint32_t g_sdio_nblks = 0;

/* SD卡信息 */
SD_CardInfo g_sd_card_info;

/* DMA传输完成标志 */
static volatile uint8_t g_sdio_dma_sta = 0;  // 0:传输中, 1:传输完成, 2:传输错误

/* 函数声明 *//* 静态函数, 仅sdio_sdcard.c内部使用 */
static void sdmmc_nvic_config(void);                                                  /* 配置SDIO相关中断向量和优先级 */
static void sdmmc_clock_set(uint16_t clkdiv);                                         /* 设置SDIO时钟分频系数，控制SD卡通信频率 */
static void sdmmc_send_cmd(uint8_t cmdindex, uint8_t waitrsp, uint32_t arg);          /* 发送SD/MMC命令，包含命令索引、响应等待标志和命令参数 */
static void sdmmc_send_data_cfg(uint32_t datatimeout, uint32_t datalen, uint8_t blksize, uint8_t dir); /* 配置数据传输参数：超时时间、数据长度、块大小和传输方向 */
static SD_Error sdmmc_power_on(void);                                                 /* 上电并初始化SDIO控制器，进入待机状态 */
static SD_Error sdmmc_initialize_cards(void);                                         /* 初始化SD卡，执行CMD0、CMD8、ACMD41等初始化序列 */
static SD_Error sdmmc_get_card_info(SD_CardInfo *cardinfo);                           /* 获取SD卡信息（容量、版本、OCR、CID、CSD等）并填充到结构体 */
static SD_Error sdmmc_wide_bus_operation(uint32_t wmode);                             /* 设置SD卡总线宽度（1位或4位模式） */
static SD_Error sdmmc_select_deselect(uint32_t addr);                                 /* 选择或取消选择指定地址的SD卡（通过RCA地址） */
static SD_Error sdmmc_read_blocks(uint8_t *pbuf, long long addr, uint16_t blksize, uint32_t nblks); /* 从SD卡读取多个数据块到指定缓冲区 */
static SD_Error sdmmc_write_blocks(uint8_t *pbuf, long long addr, uint16_t blksize, uint32_t nblks); /* 将缓冲区数据写入SD卡的多个数据块 */
static SD_Error sdmmc_cmd_error(void);                                                /* 处理SD命令传输错误（通用错误处理） */
static SD_Error sdmmc_cmd_resp1_error(uint8_t cmd);                                   /* 检查R1类型响应错误（含状态位解析） */
static SD_Error sdmmc_cmd_resp2_error(void);                                          /* 检查R2类型响应错误（CID或CSD寄存器响应） */
static SD_Error sdmmc_cmd_resp3_error(void);                                          /* 检查R3类型响应错误（OCR寄存器响应） */
static SD_Error sdmmc_cmd_resp6_error(uint8_t cmd, uint16_t *prca);                   /* 检查R6类型响应错误（发布RCA地址的响应） */
static SD_Error sdmmc_cmd_resp7_error(void);                                          /* 检查R7类型响应错误（接口条件检查响应） */
static SD_Error sdmmc_wide_bus_enable(uint8_t enx);                                   /* 启用或禁用SD卡4位宽总线模式 */
static SD_Error sdmmc_is_card_programming(uint8_t *pstatus);                          /* 查询SD卡是否处于编程（写入）状态 */
static SD_Error sdmmc_send_status(uint32_t *pstatus);                                 /* 发送CMD13命令获取SD卡当前状态 */
static SD_Error sdmmc_find_scr(uint16_t rca, uint32_t *pscr);                         /* 读取SD卡的SCR（SD配置寄存器）信息 */
static void sdmmc_dma_config(uint32_t *mbuf, uint32_t bufsize, uint32_t dir);         /* 配置DMA通道，用于SDIO数据传输的内存缓冲区和方向 */
void SDIO_IRQHandler(void);                                                           /* SDIO中断服务函数，处理SDIO数据传输完成、命令完成等中断 */
void DMA2_Stream3_IRQHandler(void);                                                   /* DMA2 Stream3中断服务函数，处理SDIO数据传输的DMA完成中断 */
/* SDIO = Secure Digital Input/Output,是 STM32F407 内部一个专门用于读写 SD/MMC 卡的外设模块,挂在 APB2 总线上(本工程 48MHz,经 CLKCR 分频后输出给卡)。*/
/* DMA = Direct Memory Access,是 STM32F407 内部一个专门用于内存和外设之间数据传输的外设模块,挂在 AHB1 总线上。*/
/**
 * @brief       SDIO和DMA中断配置
 * @param       无
 * @retval      无
 */
static void sdmmc_nvic_config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    
    /* SDIO中断配置 */
    NVIC_InitStructure.NVIC_IRQChannel = SDIO_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    /* DMA2 Stream3中断配置 */
    NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/**
 * @brief       SDIO DMA配置
 * @param       mbuf    : 数据缓冲区地址
 * @param       bufsize : 缓冲区大小（字节）
 * @param       dir     : 方向 0:内存->外设(写), 1:外设->内存(读)
 * @retval      无
 */
static void sdmmc_dma_config(uint32_t *mbuf, uint32_t bufsize, uint32_t dir)
{
    DMA_InitTypeDef DMA_InitStructure;
    
    /* 使能DMA2时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
    
    /* 复位DMA流之前先禁用它 */
    DMA_Cmd(SD_SDIO_DMA_STREAM, DISABLE);
    DMA_DeInit(SD_SDIO_DMA_STREAM);
    
    /* 等待DMA可配置 */
    while (DMA_GetCmdStatus(SD_SDIO_DMA_STREAM) != DISABLE);
    
    /* 清除DMA标志 */
    DMA_ClearFlag(SD_SDIO_DMA_STREAM, SD_SDIO_DMA_FLAG_TCIF | SD_SDIO_DMA_FLAG_TEIF | 
                  SD_SDIO_DMA_FLAG_FEIF | SD_SDIO_DMA_FLAG_DMEIF | SD_SDIO_DMA_FLAG_HTIF);
		
		/* DMA配置 */
		DMA_InitStructure.DMA_Channel = SD_SDIO_DMA_CHANNEL;  /* 指定DMA通道 */
		DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SDIO->FIFO;  /* 设置外设地址，指向SDIO的FIFO寄存器 */
		DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)mbuf;  /* 设置内存基地址，mbuf是用户提供的缓冲区指针 */
		DMA_InitStructure.DMA_DIR = dir ? DMA_DIR_PeripheralToMemory : DMA_DIR_MemoryToPeripheral;  /* 设置数据传输方向 */
		DMA_InitStructure.DMA_BufferSize = 0;  /* 缓冲区大小设为0，表示传输长度由SDIO硬件控制（通常通过SDIO数据长度寄存器设置） */
		DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;  /* 外设地址不递增，因为始终访问同一个FIFO寄存器 */
		DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;  /* 内存地址递增，使DMA能够连续填充/读取缓冲区 */
		DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;  /* 外设数据宽度：字（4字节），与SDIO FIFO宽度匹配 */
		DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;  /* 内存数据宽度：字（4字节） DMA 每个节拍搬运的 bit 数*/
		DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;  /* DMA模式：普通模式（单次传输），传输完成后停止 */
		DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;  /* DMA优先级：非常高，确保SD卡数据及时传输 */
		DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Enable;  /* 启用DMA FIFO模式，提高传输效率 */
		DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;  /* FIFO阈值：FIFO满时触发传输，优化突发传输性能 */
		DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_INC4;  /* 内存突发传输：每次突发4个节拍（beats），与32位总线宽度匹配 */
		DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_INC4;  /* 外设突发传输：每次突发4个节拍，与SDIO FIFO宽度匹配 */
		DMA_Init(SD_SDIO_DMA_STREAM, &DMA_InitStructure);  /* 初始化DMA流*/
		
    /* 设置DMA的流控制器为外设端 */
    DMA_FlowControllerConfig(SD_SDIO_DMA_STREAM, DMA_FlowCtrl_Peripheral);
		
		/* 使能DMA传输完成和错误中断 */
    DMA_ITConfig(SD_SDIO_DMA_STREAM, DMA_IT_TC | DMA_IT_TE, ENABLE);/*Transfer Complete*/ /* 使能DMA传输完成和错误中断 */
    
    /* 使能DMA流 */
    DMA_Cmd(SD_SDIO_DMA_STREAM, ENABLE);
}

/**
 * @brief       初始化SD卡
 * @param       无
 * @retval      无
 */
SD_Error sd_init(void)
{
    SD_Error errorstatus = SD_OK;
    uint8_t clkdiv = 0;
    
		/* 创建FreeRTOS同步对象 */
    if (g_sdio_transfer_sem == NULL)
    {
        g_sdio_transfer_sem = xSemaphoreCreateBinary();
        if (g_sdio_transfer_sem == NULL)return SD_ERROR;
    }
    
    if (g_sdio_mutex == NULL)
    {
        g_sdio_mutex = xSemaphoreCreateMutex();
        if (g_sdio_mutex == NULL)return SD_ERROR;
    }
		
    /* SDIO IO口初始化 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SDIO, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  /* 使能DMA2时钟 */
    
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &gpio);
/*  hai库 
    GPIO_InitTypeDef gpio;
    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9 | ... | GPIO_PIN_12;
    gpio.Mode      = GPIO_MODE_AF_PP;      // SPL 的 Mode_AF + OType_PP 合并
    gpio.Alternate = GPIO_AF12_SDIO;     把复用功能接到对应外设  // 取代 GPIO_PinAFConfig 调用
    HAL_GPIO_Init(GPIOC, &gpio);*/
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource8,  GPIO_AF_SDIO);/*意思是:把 PC8 这个引脚的复用功能号(AF)选成 SDIO*/
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource9,  GPIO_AF_SDIO);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource10, GPIO_AF_SDIO);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource11, GPIO_AF_SDIO);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource12, GPIO_AF_SDIO);

    gpio.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOD, &gpio);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource2, GPIO_AF_SDIO);
		
		/* 配置NVIC中断 */
    sdmmc_nvic_config();
    
    SDIO_DeInit(); //复位SDIO

    errorstatus = sdmmc_power_on();				/* 卡上电 SDIO_InitStructure& 识别类型 */

    if (errorstatus == SD_OK)
    {
        errorstatus = sdmmc_initialize_cards(); /* 初始化所有的卡,并让卡进入就绪状态 */
    }

    if (errorstatus == SD_OK)
    {
        errorstatus = sdmmc_get_card_info(&g_sd_card_info);/* 得到卡信息 */
    }

    if (errorstatus == SD_OK)
    {
        errorstatus = sdmmc_select_deselect((uint32_t)(g_sd_card_info.RCA << 16));/* 卡选中 */
    }

    if (errorstatus == SD_OK)
    {
        errorstatus = sdmmc_wide_bus_operation(1);				/* 设置SDIO总线宽度4 */
    }

    if ((errorstatus == SD_OK) || (MULTIMEDIA_CARD == CardType))
    {
			 /* 根据卡类型计算最佳时钟分频 */
        if (g_sd_card_info.CardType == STD_CAPACITY_SD_CARD_V1_1 || g_sd_card_info.CardType == STD_CAPACITY_SD_CARD_V2_0)
        {
            clkdiv = SDIO_TRANSFER_CLK_DIV + 2;
        }
        else
        {
            clkdiv = SDIO_TRANSFER_CLK_DIV;
        }

        sdmmc_clock_set(clkdiv); 		//SDIO 时钟设置
    }

    return errorstatus;
}

/**
 * @brief       SDIO 时钟设置
 */
static void sdmmc_clock_set(uint16_t clkdiv)
{
    uint32_t tmpreg = SDIO->CLKCR;/*按位与清零 -> 补码新的分频*/
    /*CLKDIV(低 8 位)	时钟分频系数  先取反变成FFFFFF00 再清零低八位 然后补码新的clkdiv分频*/
    tmpreg &= ~SDIO_CLKCR_CLKDIV;
    tmpreg |= (uint32_t)clkdiv;
    SDIO->CLKCR = tmpreg;
}

/**
 * @brief       SDIO 发送命令函数
 * @param       cmdindex : 命令索引,低六位有效
 * @param       waitrsp  : 期待的响应.
 *   @arg       00/10, 无响应
 *   @arg       01   , 短响应
 *   @arg       11   , 长响应
 * @param       arg      : 命令参数
 * @retval      无
 */
static void sdmmc_send_cmd(uint8_t cmdindex, uint8_t waitrsp, uint32_t arg)
{
    SDIO_CmdInitTypeDef command;
    
    command.SDIO_Argument = arg;
    command.SDIO_CmdIndex = cmdindex;
    command.SDIO_Response = (uint32_t)waitrsp << 6;
    command.SDIO_Wait = SDIO_Wait_No;
    command.SDIO_CPSM = SDIO_CPSM_Enable;/*启动状态机cpsm 在 CMD 线上组装/发送命令帧、收响应*/
    
    SDIO_SendCommand(&command);
}

/**
 * @brief       SDIO 发送数据配置函数
 * @param       datatimeout : 超时时间设置
 * @param       datalen     : 传输数据长度,低25位有效,必须为块大小的整数倍
 * @param       blksize     : 块大小. 实际大小为: 2^blksize字节
 * @param       dir         : 数据传输方向: 0, 控制器到卡; 1, 卡到控制器;
 * @retval      无
 */
static void sdmmc_send_data_cfg(uint32_t datatimeout, uint32_t datalen, uint8_t blksize, uint8_t dir)
{
    SDIO_DataInitTypeDef SDIO_DataInitStructure;
    SDIO_DataInitStructure.SDIO_DataTimeOut = datatimeout;
    SDIO_DataInitStructure.SDIO_DataLength = datalen & 0x01FFFFFF;
    SDIO_DataInitStructure.SDIO_DataBlockSize = blksize << 4;
    SDIO_DataInitStructure.SDIO_TransferDir = (dir == 0) ? SDIO_TransferDir_ToCard : SDIO_TransferDir_ToSDIO;
    SDIO_DataInitStructure.SDIO_TransferMode = SDIO_TransferMode_Block;
    SDIO_DataInitStructure.SDIO_DPSM = SDIO_DPSM_Enable;/*启动数据处理状态机dpsm 在 DAT 线上传输数据*/
    SDIO_DataConfig(&SDIO_DataInitStructure);
}

/**
 * @brief       卡上电
 *   @note      查询所有SDIO接口上的卡设备,并查询其电压和配置时钟
 * @param       无
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_power_on(void)
{
    uint8_t i = 0;
    SD_Error errorstatus = SD_OK;
    uint32_t response = 0, count = 0, validvoltage = 0;
    uint32_t SDType = SD_STD_CAPACITY;
    
		/* 配置CLKCR时钟控制寄存器 */
    SDIO_InitTypeDef SDIO_InitStructure;
    SDIO_InitStructure.SDIO_ClockDiv = SDIO_INIT_CLK_DIV; /* 时钟分频，初始化时不超过400KHz */
    SDIO_InitStructure.SDIO_ClockEdge = SDIO_ClockEdge_Rising;
    SDIO_InitStructure.SDIO_ClockBypass = SDIO_ClockBypass_Disable; 
    SDIO_InitStructure.SDIO_ClockPowerSave = SDIO_ClockPowerSave_Disable;
    SDIO_InitStructure.SDIO_BusWide = SDIO_BusWide_1b;	/* 1位数据宽度 */
    SDIO_InitStructure.SDIO_HardwareFlowControl = SDIO_HardwareFlowControl_Disable;
    SDIO_Init(&SDIO_InitStructure);
    /*tmpreg = SDIO->CLKCR;                 // 读当前值
    tmpreg &= CLKCR_CLEAR_MASK;           // 清 CLKDIV/PWRSAV/BYPASS/WIDBUS/NEGEDGE/HWFC_EN
    tmpreg |= ClockDiv | PWRSAV | BYPASS | WIDBUS | NEGEDGE | HWFC_EN;
    SDIO->CLKCR = tmpreg;                 // 写回*/
    /* 所有init流程：tmp读取寄存器 -> 掩码清除init结构体里面配置的相关位 -> 补码新的配置值 -> 写回寄存器 */
    /* 上电状态，开启卡时钟 */
    SDIO_SetPowerState(SDIO_PowerState_ON);
		/* 使能SDIO时钟 */
    SDIO_ClockCmd(ENABLE);
    
    for (i = 0; i < 74; i++)//根据SD卡物理层规范v2.0：SD卡需要至少74个时钟周期来完成复位
    {
        sdmmc_send_cmd(SD_CMD_GO_IDLE_STATE, 0, 0);/* 发送CMD0进入IDLE STAGE模式命令. CMD0（GO_IDLE_STATE）的作用是把卡复位到 Idle（空闲）状态*/
        errorstatus = sdmmc_cmd_error();
        if (errorstatus == SD_OK) break;
    }

    if (errorstatus) return errorstatus;
		
		/* 发送CMD8,短响应,检查SD卡接口特性 这条命令只有 SD 2.0 及以上版本的卡才认识*/
    sdmmc_send_cmd(SD_SDMMC_SEND_IF_COND, 1, SD_CHECK_PATTERN);
    errorstatus = sdmmc_cmd_resp7_error();/* 等待R7响应 */

    if (errorstatus == SD_OK)
    {
        CardType = STD_CAPACITY_SD_CARD_V2_0;/* SD 2.0卡 */
        SDType = SD_HIGH_CAPACITY;					 /* 高容量卡 */
    }
    /*SD 卡有一类应用专属命令 ACMD（ACMD41 就是），发 ACMD 前必须先发 CMD55 声明"下一条是 ACMD"。
    效果只对紧邻的下一条命令有效，所以循环里每轮都要重发*/
    sdmmc_send_cmd(SD_CMD_APP_CMD, 1, 0);/* 发送CMD55,短响应 */
    errorstatus = sdmmc_cmd_resp1_error(SD_CMD_APP_CMD);/* 等待R1响应 */

    if (errorstatus == SD_OK)/* SD2.0/SD 1.1,否则为MMC卡 */
    {
				/* SD卡,发送ACMD41 SD_APP_OP_COND SD 卡电压协商,参数为:0x80100000 */
        while ((!validvoltage) && (count < SD_MAX_VOLT_TRIAL))
        {
            sdmmc_send_cmd(SD_CMD_APP_CMD, 1, 0);/* 发送CMD55,短响应 */
            errorstatus = sdmmc_cmd_resp1_error(SD_CMD_APP_CMD);/* 等待R1响应 */

            if (errorstatus != SD_OK) return errorstatus;

            sdmmc_send_cmd(SD_CMD_SD_APP_OP_COND, 1, SD_VOLTAGE_WINDOW_SD | SDType);/* 发送ACMD41,短响应 */
            errorstatus = sdmmc_cmd_resp3_error();		/* 等待R3响应 */

            if (errorstatus != SD_OK) return errorstatus;

            response = SDIO_GetResponse(SDIO_RESP1);/* 得到响应 */
            validvoltage = (((response >> 31) == 1) ? 1 : 0);/* 判断SD卡上电是否完成 */
            count++;
        }
				
				/* 检查是否超过最大电压协商尝试次数 */
        if (count >= SD_MAX_VOLT_TRIAL)
        {
            errorstatus = SD_INVALID_VOLTRANGE;
            return errorstatus;
        }
				/* 检查SD卡是否支持高容量（SDHC/SDXC）*/
        if (response & SD_HIGH_CAPACITY)
        {
            CardType = HIGH_CAPACITY_SD_CARD;
        }
    }
    else/* MMC卡 */
    {
				/* MMC卡,发送CMD1 SDMMC_SEND_OP_COND,参数为:0x80FF8000 */
        while ((!validvoltage) && (count < SD_MAX_VOLT_TRIAL))
        {
            sdmmc_send_cmd(SD_CMD_SEND_OP_COND, 1, SD_VOLTAGE_WINDOW_MMC);/* 发送CMD1,短响应 */
            errorstatus = sdmmc_cmd_resp3_error();/* 等待R3响应 */

            if (errorstatus != SD_OK) return errorstatus;

            response = SDIO_GetResponse(SDIO_RESP1);
            validvoltage = (((response >> 31) == 1) ? 1 : 0);/* 判断SD卡上电是否完成 */
            count++;
        }

        if (count >= SD_MAX_VOLT_TRIAL)
        {
            errorstatus = SD_INVALID_VOLTRANGE;
            return errorstatus;
        }

        CardType = MULTIMEDIA_CARD;
    }

    return (errorstatus);
}

/**
 * @brief       初始化所有的卡,并让卡进入就绪状态
 */
static SD_Error sdmmc_initialize_cards(void)
{
    SD_Error errorstatus = SD_OK;
    uint16_t rca = 0x01;
	
		/* 检查电源状态,确保为上电状态 */
    if (SDIO_GetPowerState() == 0x00)
    {
        return SD_REQUEST_NOT_APPLICABLE;
    }
		
		/* 非SECURE_DIGITAL_IO_CARD */
    if (SECURE_DIGITAL_IO_CARD != CardType)
    {
        sdmmc_send_cmd(SD_CMD_ALL_SEND_CID, 3, 0);	 /* 发送CMD2,取得CID,Card Identification Number长响应 */
        errorstatus = sdmmc_cmd_resp2_error();		/* 等待R2响应 */

        if (errorstatus != SD_OK) return errorstatus;

        CID_Tab[0] = SDIO_GetResponse(SDIO_RESP1);/* 获取Card identification number data */
        CID_Tab[1] = SDIO_GetResponse(SDIO_RESP2);
        CID_Tab[2] = SDIO_GetResponse(SDIO_RESP3);
        CID_Tab[3] = SDIO_GetResponse(SDIO_RESP4);
    }
		
		/* 判断卡类型 */
    if ((STD_CAPACITY_SD_CARD_V1_1 == CardType) || (STD_CAPACITY_SD_CARD_V2_0 == CardType) || (SECURE_DIGITAL_IO_COMBO_CARD == CardType) || (HIGH_CAPACITY_SD_CARD == CardType))
    {
        sdmmc_send_cmd(SD_CMD_SET_REL_ADDR, 1, 0);		/* 发送CMD3,短响应 */
        errorstatus = sdmmc_cmd_resp6_error(SD_CMD_SET_REL_ADDR, &rca); /* 等待R6响应 */
        /*让卡发布一个 RCA（Relative Card Address，16 位相对地址）。SD 卡模式下参数为 0，意思是"地址你（卡）自己挑一个"，卡在 R6 响应里把选好的 RCA 报回来，驱动从 rspr1 >> 16 取出来存进 rca（1067 行）。
        之后所有"点对点"命令都要用 RCA << 16 作为参数来寻址这张卡。
        /*收到 CMD3 后，卡从 Identification 进入 Stand-by（待命）状态。*/
        if (errorstatus != SD_OK) return errorstatus;
    }

    if (MULTIMEDIA_CARD == CardType)
    {
        sdmmc_send_cmd(SD_CMD_SET_REL_ADDR, 1, (uint32_t)(rca << 16));/* 发送CMD3,短响应 */
        errorstatus = sdmmc_cmd_resp2_error();		/* 等待R2响应 */

        if (errorstatus != SD_OK) return errorstatus;
    }

    if (SECURE_DIGITAL_IO_CARD != CardType)
    {
        RCA = rca;
        sdmmc_send_cmd(SD_CMD_SEND_CSD, 3, (uint32_t)(rca << 16));/* 发送CMD9+卡RCA,取得CSD,长响应 */
        errorstatus = sdmmc_cmd_resp2_error();        /* 等待R2响应 */

        if (errorstatus != SD_OK) return errorstatus;

        CSD_Tab[0] = SDIO_GetResponse(SDIO_RESP1);/* 获取Card Specific Data */
        CSD_Tab[1] = SDIO_GetResponse(SDIO_RESP2);
        CSD_Tab[2] = SDIO_GetResponse(SDIO_RESP3);
        CSD_Tab[3] = SDIO_GetResponse(SDIO_RESP4);
        /*寻址命令：参数里带上刚拿到的 RCA（左移 16 位放高 16 位），只让这一张卡应答。
        卡回 R2 长响应 = CSD（Card-Specific Data，128 位描述卡特性的寄存器：容量、块大小、最高时钟、读写参数等）。
        数据存进 CSD_Tab[0..3]（455-458 行），后面 sdmmc_get_card_info() 就是逐位解析 CSD，算出 CardCapacity（卡容量）、CardBlockSize 等信息。*/
    }

    return SD_OK;			/* 卡初始化成功 */
}

/**
 * @brief       得到卡信息
 * @param       cardinfo : 卡信息存储结构体指针
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_get_card_info(SD_CardInfo *cardinfo)
{
    SD_Error errorstatus = SD_OK;
    uint8_t tmp = 0;
    cardinfo->CardType = (uint8_t)CardType;             /* 卡类型 */
    cardinfo->RCA = (uint16_t)RCA;                      /* 卡RCA值 */
    tmp = (uint8_t)((CSD_Tab[0] & 0xFF000000) >> 24);
    cardinfo->SD_csd.CSDStruct = (tmp & 0xC0) >> 6;     /* CSD结构 */
    cardinfo->SD_csd.SysSpecVersion = (tmp & 0x3C) >> 2;/* 2.0协议还没定义这部分(为保留),应该是后续协议定义的 */
    cardinfo->SD_csd.Reserved1 = tmp & 0x03;            /* 2个保留位 */
    tmp = (uint8_t)((CSD_Tab[0] & 0x00FF0000) >> 16);   /* 第1个字节 */
    cardinfo->SD_csd.TAAC = tmp;                        /* 数据读时间1 */
    tmp = (uint8_t)((CSD_Tab[0] & 0x0000FF00) >> 8);    /* 第2个字节 */
    cardinfo->SD_csd.NSAC = tmp;                        /* 数据读时间2 */
    tmp = (uint8_t)(CSD_Tab[0] & 0x000000FF);           /* 第3个字节 */
    cardinfo->SD_csd.MaxBusClkFrec = tmp;               /* 传输速度 */
    tmp = (uint8_t)((CSD_Tab[1] & 0xFF000000) >> 24);   /* 第4个字节 */
    cardinfo->SD_csd.CardComdClasses = tmp << 4;        /* 卡指令类高四位 */
    tmp = (uint8_t)((CSD_Tab[1] & 0x00FF0000) >> 16);   /* 第5个字节 */
    cardinfo->SD_csd.CardComdClasses |= (tmp & 0xF0) >> 4;  /* 卡指令类低四位 */
    cardinfo->SD_csd.RdBlockLen = tmp & 0x0F;           /* 最大读取数据长度 */
    tmp = (uint8_t)((CSD_Tab[1] & 0x0000FF00) >> 8);    /* 第6个字节 */
    cardinfo->SD_csd.PartBlockRead = (tmp & 0x80) >> 7; /* 允许分块读 */
    cardinfo->SD_csd.WrBlockMisalign = (tmp & 0x40) >> 6;   /* 写块错位 */
    cardinfo->SD_csd.RdBlockMisalign = (tmp & 0x20) >> 5;   /* 读块错位 */
    cardinfo->SD_csd.DSRImpl = (tmp & 0x10) >> 4;
    cardinfo->SD_csd.Reserved2 = 0;                     /* 保留 */

    /* 标准1.1/2.0卡/MMC卡 */
    if ((CardType == STD_CAPACITY_SD_CARD_V1_1) || (CardType == STD_CAPACITY_SD_CARD_V2_0) || (MULTIMEDIA_CARD == CardType))
    {
        cardinfo->SD_csd.DeviceSize = (tmp & 0x03) << 10;   /* C_SIZE(12位) */
        tmp = (uint8_t)(CSD_Tab[1] & 0x000000FF);           /* 第7个字节 */
        cardinfo->SD_csd.DeviceSize |= (tmp) << 2;
        tmp = (uint8_t)((CSD_Tab[2] & 0xFF000000) >> 24);   /* 第8个字节 */
        cardinfo->SD_csd.DeviceSize |= (tmp & 0xC0) >> 6;
        cardinfo->SD_csd.MaxRdCurrentVDDMin = (tmp & 0x38) >> 3;
        cardinfo->SD_csd.MaxRdCurrentVDDMax = (tmp & 0x07);
        tmp = (uint8_t)((CSD_Tab[2] & 0x00FF0000) >> 16);   /* 第9个字节 */
        cardinfo->SD_csd.MaxWrCurrentVDDMin = (tmp & 0xE0) >> 5;
        cardinfo->SD_csd.MaxWrCurrentVDDMax = (tmp & 0x1C) >> 2;
        cardinfo->SD_csd.DeviceSizeMul = (tmp & 0x03) << 1; /* C_SIZE_MULT */
        tmp = (uint8_t)((CSD_Tab[2] & 0x0000FF00) >> 8);    /* 第10个字节 */
        cardinfo->SD_csd.DeviceSizeMul |= (tmp & 0x80) >> 7;
        cardinfo->CardCapacity = (cardinfo->SD_csd.DeviceSize + 1);     /* 计算卡容量 */
        cardinfo->CardCapacity *= (1 << (cardinfo->SD_csd.DeviceSizeMul + 2));
        cardinfo->CardBlockSize = 1 << (cardinfo->SD_csd.RdBlockLen);   /* 块大小 */
        cardinfo->CardCapacity *= cardinfo->CardBlockSize;
    }
    else if (CardType == HIGH_CAPACITY_SD_CARD) /* 高容量卡 */
    {
        tmp = (uint8_t)(CSD_Tab[1] & 0x000000FF);           /* 第7个字节 */
        cardinfo->SD_csd.DeviceSize = (tmp & 0x3F) << 16;   /* C_SIZE */
        tmp = (uint8_t)((CSD_Tab[2] & 0xFF000000) >> 24);   /* 第8个字节 */
        cardinfo->SD_csd.DeviceSize |= (tmp << 8);
        tmp = (uint8_t)((CSD_Tab[2] & 0x00FF0000) >> 16);   /* 第9个字节 */
        cardinfo->SD_csd.DeviceSize |= (tmp);
        tmp = (uint8_t)((CSD_Tab[2] & 0x0000FF00) >> 8);    /* 第10个字节 */
        cardinfo->CardCapacity = (long long)(cardinfo->SD_csd.DeviceSize + 1) * 512 * 1024; /* 计算卡容量 */
        cardinfo->CardBlockSize = 512;                      /* 块大小固定为512字节 */
    }

    cardinfo->SD_csd.EraseGrSize = (tmp & 0x40) >> 6;
    cardinfo->SD_csd.EraseGrMul = (tmp & 0x3F) << 1;
    tmp = (uint8_t)(CSD_Tab[2] & 0x000000FF);               /* 第11个字节 */
    cardinfo->SD_csd.EraseGrMul |= (tmp & 0x80) >> 7;
    cardinfo->SD_csd.WrProtectGrSize = (tmp & 0x7F);
    tmp = (uint8_t)((CSD_Tab[3] & 0xFF000000) >> 24);       /* 第12个字节 */
    cardinfo->SD_csd.WrProtectGrEnable = (tmp & 0x80) >> 7;
    cardinfo->SD_csd.ManDeflECC = (tmp & 0x60) >> 5;
    cardinfo->SD_csd.WrSpeedFact = (tmp & 0x1C) >> 2;
    cardinfo->SD_csd.MaxWrBlockLen = (tmp & 0x03) << 2;
    tmp = (uint8_t)((CSD_Tab[3] & 0x00FF0000) >> 16);       /* 第13个字节 */
    cardinfo->SD_csd.MaxWrBlockLen |= (tmp & 0xC0) >> 6;
    cardinfo->SD_csd.WriteBlockPaPartial = (tmp & 0x20) >> 5;
    cardinfo->SD_csd.Reserved3 = 0;
    cardinfo->SD_csd.ContentProtectAppli = (tmp & 0x01);
    tmp = (uint8_t)((CSD_Tab[3] & 0x0000FF00) >> 8);        /* 第14个字节 */
    cardinfo->SD_csd.FileFormatGrouop = (tmp & 0x80) >> 7;
    cardinfo->SD_csd.CopyFlag = (tmp & 0x40) >> 6;
    cardinfo->SD_csd.PermWrProtect = (tmp & 0x20) >> 5;
    cardinfo->SD_csd.TempWrProtect = (tmp & 0x10) >> 4;
    cardinfo->SD_csd.FileFormat = (tmp & 0x0C) >> 2;
    cardinfo->SD_csd.ECC = (tmp & 0x03);
    tmp = (uint8_t)(CSD_Tab[3] & 0x000000FF);               /* 第15个字节 */
    cardinfo->SD_csd.CSD_CRC = (tmp & 0xFE) >> 1;
    cardinfo->SD_csd.Reserved4 = 1;
    tmp = (uint8_t)((CID_Tab[0] & 0xFF000000) >> 24);       /* 第0个字节 */
    cardinfo->SD_cid.ManufacturerID = tmp;
    tmp = (uint8_t)((CID_Tab[0] & 0x00FF0000) >> 16);       /* 第1个字节 */
    cardinfo->SD_cid.OEM_AppliID = tmp << 8;
    tmp = (uint8_t)((CID_Tab[0] & 0x000000FF00) >> 8);      /* 第2个字节 */
    cardinfo->SD_cid.OEM_AppliID |= tmp;
    tmp = (uint8_t)(CID_Tab[0] & 0x000000FF);               /* 第3个字节 */
    cardinfo->SD_cid.ProdName1 = tmp << 24;
    tmp = (uint8_t)((CID_Tab[1] & 0xFF000000) >> 24);       /* 第4个字节 */
    cardinfo->SD_cid.ProdName1 |= tmp << 16;
    tmp = (uint8_t)((CID_Tab[1] & 0x00FF0000) >> 16);       /* 第5个字节 */
    cardinfo->SD_cid.ProdName1 |= tmp << 8;
    tmp = (uint8_t)((CID_Tab[1] & 0x0000FF00) >> 8);        /* 第6个字节 */
    cardinfo->SD_cid.ProdName1 |= tmp;
    tmp = (uint8_t)(CID_Tab[1] & 0x000000FF);               /* 第7个字节 */
    cardinfo->SD_cid.ProdName2 = tmp;
    tmp = (uint8_t)((CID_Tab[2] & 0xFF000000) >> 24);       /* 第8个字节 */
    cardinfo->SD_cid.ProdRev = tmp;
    tmp = (uint8_t)((CID_Tab[2] & 0x00FF0000) >> 16);       /* 第9个字节 */
    cardinfo->SD_cid.ProdSN = tmp << 24;
    tmp = (uint8_t)((CID_Tab[2] & 0x0000FF00) >> 8);        /* 第10个字节 */
    cardinfo->SD_cid.ProdSN |= tmp << 16;
    tmp = (uint8_t)(CID_Tab[2] & 0x000000FF);               /* 第11个字节 */
    cardinfo->SD_cid.ProdSN |= tmp << 8;
    tmp = (uint8_t)((CID_Tab[3] & 0xFF000000) >> 24);       /* 第12个字节 */
    cardinfo->SD_cid.ProdSN |= tmp;
    tmp = (uint8_t)((CID_Tab[3] & 0x00FF0000) >> 16);       /* 第13个字节 */
    cardinfo->SD_cid.Reserved1 |= (tmp & 0xF0) >> 4;
    cardinfo->SD_cid.ManufactDate = (tmp & 0x0F) << 8;
    tmp = (uint8_t)((CID_Tab[3] & 0x0000FF00) >> 8);        /* 第14个字节 */
    cardinfo->SD_cid.ManufactDate |= tmp;
    tmp = (uint8_t)(CID_Tab[3] & 0x000000FF);               /* 第15个字节 */
    cardinfo->SD_cid.CID_CRC = (tmp & 0xFE) >> 1;
    cardinfo->SD_cid.Reserved2 = 1;
    return errorstatus;
}

/**
 * @brief       设置SDIO总线宽度(MMC卡不支持4bit模式)
 * @param       wmode  : 位宽模式
 *   @arg       0, 1位数据宽度;
 *   @arg       1, 4位数据宽度;
 *   @arg       2, 8位数据宽度
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_wide_bus_operation(uint32_t wmode)
{
    SD_Error errorstatus = SD_OK;
    uint32_t tmpreg = 0;
    
		 /* MMC卡不支持总线宽度设置 */
    if (MULTIMEDIA_CARD == CardType)
    {
        return SD_UNSUPPORTED_FEATURE;
    }
    
		/* 检查卡类型是否支持总线宽度设置 */
    if ((STD_CAPACITY_SD_CARD_V1_1 == CardType) || 
        (STD_CAPACITY_SD_CARD_V2_0 == CardType) || 
        (HIGH_CAPACITY_SD_CARD == CardType))
    {
				/* SD卡不支持8位模式 */
        if (wmode >= 2)
        {
            return SD_UNSUPPORTED_FEATURE;
        }
        
        errorstatus = sdmmc_wide_bus_enable(wmode);/* 发送ACMD6命令使能宽总线模式 */
        
        if (SD_OK == errorstatus)
        {
            tmpreg = SDIO->CLKCR;            /* 读取当前值 */
            tmpreg &= ~SDIO_CLKCR_WIDBUS;    /* 清除总线宽度位 */
            
            switch (wmode)
            {
                case 0:/* 1位总线 */
                    tmpreg |= SDIO_BusWide_1b; 
                    break;
                case 1: /* 4位总线 */
                    tmpreg |= SDIO_BusWide_4b; 
                    break;
                default:
                    return SD_UNSUPPORTED_FEATURE;
            }
            
            SDIO->CLKCR = tmpreg;            /* 写回寄存器 */
        }
    }   
    return errorstatus;
}

/**
 * @brief       卡选中
 *   @note      发送CMD7,选择相对地址(rca)为addr的卡,取消其他卡.如果为0,则都不选择.
 * @param       addr : 卡的RCA地址
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_select_deselect(uint32_t addr)
{
    sdmmc_send_cmd(SD_CMD_SEL_DESEL_CARD, 1, addr);/* 发送CMD7,选择卡,短响应 */
    return sdmmc_cmd_resp1_error(SD_CMD_SEL_DESEL_CARD);
}

/**
 * @brief       SDIO DMA读取单个/多个块
 * @param       pbuf    : 读数据缓存区
 * @param       addr    : 读地址
 * @param       blksize : 块大小
 * @param       nblks   : 要读的块数
 * @retval      0, 正常;  其他, 错误代码
 */
static SD_Error sdmmc_read_blocks(uint8_t *pbuf, long long addr, uint16_t blksize, uint32_t nblks)
{
    SD_Error errorstatus = SD_OK;
    
    /* 数据控制寄存器清零 */
    SDIO_DataInitTypeDef data_config;
    SDIO_DataStructInit(&data_config);
    SDIO_DataConfig(&data_config);

    if (CardType == HIGH_CAPACITY_SD_CARD) /* 大容量卡 */
    {
        blksize = 512;
        addr >>= 9;
    }

    /* 设置块大小 */
    sdmmc_send_cmd(SD_CMD_SET_BLOCKLEN, 1, blksize);   				/* 发送CMD16+设置数据长度为blksize,短响应 */
    errorstatus = sdmmc_cmd_resp1_error(SD_CMD_SET_BLOCKLEN); /* 等待R1响应 */
    if (errorstatus != SD_OK) return errorstatus;
		
		/* 清除传输标志 */
    g_sdio_transfer_end = 0;
    g_sdio_transfer_error = 0;
    g_sdio_error = SD_OK;
    g_sdio_nblks = nblks;

    /* 配置DMA - 读操作：外设到内存 */
    sdmmc_dma_config((uint32_t *)pbuf, nblks * blksize, 1);
    
    /* 使能SDIO DMA */
    SDIO_DMACmd(ENABLE);
    
    /* 配置数据传输SDIO_CmdInitTypeDef */
    sdmmc_send_data_cfg(SD_DATATIMEOUT, nblks * blksize, 9, 1);/* nblks*blksize,块大小恒为512,卡到控制器 */
		
		/* 发送读命令 */
    if (nblks > 1)/* 多块读 */
    {
        sdmmc_send_cmd(SD_CMD_READ_MULT_BLOCK, 1, addr);		 /* 发送CMD18+从addr地址出读取数据,短响应 */
        errorstatus = sdmmc_cmd_resp1_error(SD_CMD_READ_MULT_BLOCK);  /* 等待R1响应 */
    }
    else /* 单块读 */
    {
        sdmmc_send_cmd(SD_CMD_READ_SINGLE_BLOCK, 1, addr);      /* 发送CMD17+从addr地址出读取数据,短响应 */
        errorstatus = sdmmc_cmd_resp1_error(SD_CMD_READ_SINGLE_BLOCK);  /* 等待R1响应 */
    }
		
		if (errorstatus != SD_OK)
		{
				SDIO_DMACmd(DISABLE);
				DMA_Cmd(SD_SDIO_DMA_STREAM, DISABLE);
				return errorstatus;
		}

		/* 使能SDIO相关中断 */
    SDIO_ITConfig(SDIO_IT_DATAEND | SDIO_IT_DTIMEOUT | SDIO_IT_DCRCFAIL | SDIO_IT_RXOVERR, ENABLE);
		  
    /* 等待传输完成信号量 */
    if (xSemaphoreTake(g_sdio_transfer_sem, pdMS_TO_TICKS(SDIO_TRANSFER_TIMEOUT_MS)) != pdTRUE)
    {
        /* 超时处理 */
        SDIO_ITConfig(SDIO_IT_DATAEND | SDIO_IT_DTIMEOUT | SDIO_IT_DCRCFAIL | SDIO_IT_RXOVERR, DISABLE);
        SDIO_DMACmd(DISABLE);
        DMA_Cmd(SD_SDIO_DMA_STREAM, DISABLE);
        return SD_DATA_TIMEOUT;
    }
		
		/* 禁用中断 */
    SDIO_ITConfig(SDIO_IT_DATAEND | SDIO_IT_DTIMEOUT | SDIO_IT_DCRCFAIL | SDIO_IT_RXOVERR, DISABLE);
    
    /* 禁用DMA */
    SDIO_DMACmd(DISABLE);
    DMA_Cmd(SD_SDIO_DMA_STREAM, DISABLE);
		
		/* 检查错误 */
    if (g_sdio_transfer_error)
    {
        return g_sdio_error;
    }

    SDIO_ClearFlag(SDIO_FLAG_ALL);   /* 清除所有标记 */
    return SD_OK;
}

/**
 * @brief       SDIO DMA写单个/多个块（中断方式）
 * @param       pbuf    : 写数据缓存区
 * @param       addr    : 写地址
 * @param       blksize : 块大小
 * @param       nblks   : 要写的块数
 * @retval      0, 正常;  其他, 错误代码
 */
static SD_Error sdmmc_write_blocks(uint8_t *pbuf, long long addr, uint16_t blksize, uint32_t nblks)
{
    SD_Error errorstatus = SD_OK;
    uint8_t  cardstate = 0;
    uint32_t timeout = 0;
    uint32_t cardstatus = 0;

    if (pbuf == NULL) return SD_INVALID_PARAMETER; /* 参数错误 */

    /* 数据控制寄存器清零 */
    SDIO_DataInitTypeDef data_config;
    SDIO_DataStructInit(&data_config);
    SDIO_DataConfig(&data_config);

    if (CardType == HIGH_CAPACITY_SD_CARD)        /* 大容量卡 */
    {
        blksize = 512;
        addr >>= 9;
    }

    /* 设置块大小 */
    sdmmc_send_cmd(SD_CMD_SET_BLOCKLEN, 1, blksize);		/* 发送CMD16+设置数据长度为blksize,短响应 */
    errorstatus = sdmmc_cmd_resp1_error(SD_CMD_SET_BLOCKLEN);  /* 等待R1响应 */
    if (errorstatus != SD_OK) return errorstatus;

    if (nblks > 1) /* 多块写 */
    {
        if (nblks * blksize > SD_MAX_DATA_LENGTH) return SD_INVALID_PARAMETER;

        if ((STD_CAPACITY_SD_CARD_V1_1 == CardType) || 
            (STD_CAPACITY_SD_CARD_V2_0 == CardType) || 
            (HIGH_CAPACITY_SD_CARD == CardType))
        {
            sdmmc_send_cmd(SD_CMD_APP_CMD, 1, (uint32_t)RCA << 16); /* 发送ACMD55,短响应 */
            errorstatus = sdmmc_cmd_resp1_error(SD_CMD_APP_CMD); 		/* 等待R1响应 */
            if (errorstatus != SD_OK) return errorstatus;

            sdmmc_send_cmd(SD_CMD_SET_BLOCK_COUNT, 1, nblks);   /* 发送CMD23,设置块数量,短响应 */
            errorstatus = sdmmc_cmd_resp1_error(SD_CMD_SET_BLOCK_COUNT); /* 等待R1响应 */
            if (errorstatus != SD_OK) return errorstatus;
        }

        sdmmc_send_cmd(SD_CMD_WRITE_MULT_BLOCK, 1, addr);  /* 发送CMD25,多块写指令,短响应 */
        errorstatus = sdmmc_cmd_resp1_error(SD_CMD_WRITE_MULT_BLOCK);  /* 等待R1响应 */
    }
    else   /* 单块写 */
    {
        sdmmc_send_cmd(SD_CMD_SEND_STATUS, 1, (uint32_t)RCA << 16); /* 发送CMD13,查询卡的状态,短响应 */
        errorstatus = sdmmc_cmd_resp1_error(SD_CMD_SEND_STATUS); 	 /* 等待R1响应 */
        if (errorstatus != SD_OK) return errorstatus;

        cardstatus = SDIO_GetResponse(SDIO_RESP1);
        timeout = SD_DATATIMEOUT;

        while (((cardstatus & 0x00000100) == 0) && (timeout > 0))  /* 检查READY_FOR_DATA位是否置位 */
        {
            timeout--;
            sdmmc_send_cmd(SD_CMD_SEND_STATUS, 1, (uint32_t)RCA << 16); /* 发送CMD13,查询卡的状态,短响应 */
            errorstatus = sdmmc_cmd_resp1_error(SD_CMD_SEND_STATUS); 		/* 等待R1响应 */
            if (errorstatus != SD_OK) return errorstatus;
            cardstatus = SDIO_GetResponse(SDIO_RESP1);
        }

        if (timeout == 0) return SD_ERROR;

        sdmmc_send_cmd(SD_CMD_WRITE_SINGLE_BLOCK, 1, addr);  						/* 发送CMD24,写单块指令,短响应 */
        errorstatus = sdmmc_cmd_resp1_error(SD_CMD_WRITE_SINGLE_BLOCK); /* 等待R1响应 */
    }

    if (errorstatus != SD_OK) return errorstatus;

    /* 清除传输标志 */
    g_sdio_transfer_end = 0;
    g_sdio_transfer_error = 0;
    g_sdio_error = SD_OK;
    g_sdio_nblks = nblks;
    
    /* 配置DMA - 写操作：内存到外设 */
    sdmmc_dma_config((uint32_t *)pbuf, nblks * blksize, 0);
    
    /* 使能SDIO DMA */
    SDIO_DMACmd(ENABLE);
    
    /* 配置数据传输 */
    sdmmc_send_data_cfg(SD_DATATIMEOUT, nblks * blksize, 9, 0);

    /* 使能SDIO相关中断 */
    SDIO_ITConfig(SDIO_IT_DATAEND | SDIO_IT_DTIMEOUT | SDIO_IT_DCRCFAIL | SDIO_IT_TXUNDERR, ENABLE);

    /* 等待传输完成信号量 */
    if (xSemaphoreTake(g_sdio_transfer_sem, pdMS_TO_TICKS(SDIO_TRANSFER_TIMEOUT_MS)) != pdTRUE)
    {
        /* 超时处理 */
        SDIO_ITConfig(SDIO_IT_DATAEND | SDIO_IT_DTIMEOUT | SDIO_IT_DCRCFAIL | SDIO_IT_TXUNDERR, DISABLE);
        SDIO_DMACmd(DISABLE);
        DMA_Cmd(SD_SDIO_DMA_STREAM, DISABLE);
        return SD_DATA_TIMEOUT;
    }

    /* 禁用中断 */
    SDIO_ITConfig(SDIO_IT_DATAEND | SDIO_IT_DTIMEOUT | SDIO_IT_DCRCFAIL | SDIO_IT_TXUNDERR, DISABLE);
    
    /* 禁用DMA */
    SDIO_DMACmd(DISABLE);
    DMA_Cmd(SD_SDIO_DMA_STREAM, DISABLE);
    
    /* 检查错误 */
    if (g_sdio_transfer_error)
    {
        return g_sdio_error;
    }

    SDIO_ClearFlag(SDIO_FLAG_ALL);

    /* 等待卡编程完成 */
    errorstatus = sdmmc_is_card_programming(&cardstate);
    while ((errorstatus == SD_OK) && 
           ((cardstate == SD_CARD_PROGRAMMING) || (cardstate == SD_CARD_RECEIVING)))
    {
        errorstatus = sdmmc_is_card_programming(&cardstate);
    }

    return errorstatus;
}

/**
 * @brief       检查CMD0的执行状态
 */
static SD_Error sdmmc_cmd_error(void)
{
    SD_Error errorstatus = SD_OK;
    uint32_t timeout = SDMMC_CMD0TIMEOUT;

    while (timeout--)
    {
        if (SDIO_GetFlagStatus(SDIO_FLAG_CMDSENT) == SET) /* 检查命令已发送标志 */
        {
            break;/* 命令已发送(无需响应) */
        }
    }

    if (timeout == 0) return SD_CMD_RSP_TIMEOUT;

    SDIO_ClearFlag(SDIO_FLAG_ALL); /* 清除标记 */
    return errorstatus;
}

/**
 * @brief       检查R1响应的错误状态
 * @param       cmd : 当前命令
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_cmd_resp1_error(uint8_t cmd)
{
    uint32_t status;
    uint32_t timeout = SDMMC_CMD1TIMEOUT;

    while (timeout--)
    {
        status = SDIO->STA;
				/* 检查：CRC错误 / 命令响应超时 / 响应接收完成 */
        if (status & (SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CTIMEOUT | SDIO_FLAG_CMDREND))
        {
            break;
        }
    }
		
		/* 响应超时处理 */
    if ((timeout == 0) || (status & SDIO_FLAG_CTIMEOUT))
    {
        SDIO_ClearFlag(SDIO_FLAG_CTIMEOUT);   /* 清除命令响应超时标志 */
        SDIO_ClearFlag(SDIO_FLAG_ALL);      	/* 清除所有标志 */
        return SD_CMD_RSP_TIMEOUT;
    }
		
		/* CRC错误处理 */
    if (status & SDIO_FLAG_CCRCFAIL)
    {
        SDIO_ClearFlag(SDIO_FLAG_CCRCFAIL);
        return SD_CMD_CRC_FAIL;
    }

		/* 检查响应命令索引是否匹配 */
    if (SDIO_GetCommandResponse() != cmd)
    {
        SDIO_ClearFlag(SDIO_FLAG_ALL);
        return SD_ILLEGAL_CMD;
    }

    SDIO_ClearFlag(SDIO_FLAG_ALL);
    return (SD_Error)(SDIO_GetResponse(SDIO_RESP1) & SD_OCR_ERRORBITS);
}

/**
 * @brief       检查R2响应的错误状态
 * @param       无
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_cmd_resp2_error(void)
{
    SD_Error errorstatus = SD_OK;
    uint32_t status;
    uint32_t timeout = SDMMC_CMD1TIMEOUT;

    while (timeout--)
    {
        status = SDIO->STA;
				/* 检查：CRC错误 / 命令响应超时 / 响应接收完成 */
        if (status & (SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CTIMEOUT | SDIO_FLAG_CMDREND))
        {
            break;
        }
    }

    if ((timeout == 0) || (status & SDIO_FLAG_CTIMEOUT))/* 响应超时 */
    {
        errorstatus = SD_CMD_RSP_TIMEOUT;
        SDIO->ICR = SDIO_FLAG_CTIMEOUT;
        return errorstatus;
    }

    if (status & SDIO_FLAG_CCRCFAIL) /* CRC错误 */
    {
        errorstatus = SD_CMD_CRC_FAIL;
        SDIO->ICR = SDIO_FLAG_CCRCFAIL;
    }

    SDIO_ClearFlag(SDIO_FLAG_ALL);
    return errorstatus;
}

/**
 * @brief       检查R3响应的错误状态（标准库最小改动版）
 * @param       无
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_cmd_resp3_error(void)
{
    uint32_t status;
    uint32_t timeout = SDMMC_CMD1TIMEOUT;

    while (timeout--)
    {
        status = SDIO->STA;
				 /* 检查：CRC错误 / 命令响应超时 / 响应接收完成 */
        if (status & (SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CTIMEOUT | SDIO_FLAG_CMDREND))
        {
            break;
        }
    }

    if ((timeout == 0) || (status & SDIO_FLAG_CTIMEOUT))/* 响应超时 */
    {
        SDIO->ICR = SDIO_FLAG_CTIMEOUT;
        return SD_CMD_RSP_TIMEOUT;
    }

    SDIO_ClearFlag(SDIO_FLAG_ALL);
    return SD_OK;
}

/**
 * @brief       检查R6响应的错误状态
 * @param       cmd : 当前命令
 * @param       prca: 卡返回的RCA地址
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_cmd_resp6_error(uint8_t cmd, uint16_t *prca)
{
    SD_Error errorstatus = SD_OK;
    uint32_t status;
    uint32_t rspr1;
    uint32_t timeout = SDMMC_CMD1TIMEOUT;

    while (timeout--)
    {
        status = SDIO->STA;
				/* 检查：CRC错误 / 命令响应超时 / 响应接收完成 */
        if (status & (SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CTIMEOUT | SDIO_FLAG_CMDREND))
        {
            break;
        }
    }

    if ((timeout == 0) || (status & SDIO_FLAG_CTIMEOUT)) /* 检查超时 */
    {
        SDIO_ClearFlag(SDIO_FLAG_CTIMEOUT);
        return SD_CMD_RSP_TIMEOUT;
    }

    if (status & SDIO_FLAG_CCRCFAIL)/* 检查CRC错误 */
    {
        SDIO_ClearFlag(SDIO_FLAG_CCRCFAIL);
        return SD_CMD_CRC_FAIL;
    }

    if (SDIO_GetCommandResponse() != cmd) /* 检查命令索引是否匹配 */
    {
        SDIO_ClearFlag(SDIO_FLAG_ALL);
        return SD_ILLEGAL_CMD;
    }

    SDIO_ClearFlag(SDIO_FLAG_ALL);
    rspr1 = SDIO_GetResponse(SDIO_RESP1);      /* 得到响应 */

    if (SD_ALLZERO == (rspr1 & (SD_R6_GENERAL_UNKNOWN_ERROR | SD_R6_ILLEGAL_CMD | SD_R6_COM_CRC_FAILED)))/* 检查R6响应中的错误位 */
    {
        *prca = (uint16_t)(rspr1 >> 16); /* 右移16位得到,rca */
        return errorstatus;
    }

			/* 根据错误位返回具体错误 */
    if (rspr1 & SD_R6_GENERAL_UNKNOWN_ERROR) return SD_GENERAL_UNKNOWN_ERROR;
    if (rspr1 & SD_R6_ILLEGAL_CMD) return SD_ILLEGAL_CMD;
    if (rspr1 & SD_R6_COM_CRC_FAILED) return SD_COM_CRC_FAILED;

    return errorstatus;
}

/**
 * @brief       检查R7响应的错误状态
 */
static SD_Error sdmmc_cmd_resp7_error(void)
{
    SD_Error errorstatus = SD_OK;
    uint32_t status;
    uint32_t timeout = SDMMC_CMD0TIMEOUT;

    while (timeout--)
    {
        status = SDIO->STA;
				/* 检查：CRC错误(1<<0) / 命令响应超时(1<<2) / 响应接收完成(1<<6) */
        if (status & (SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CTIMEOUT | SDIO_FLAG_CMDREND))
        {
            break;
        }
    }

    if ((timeout == 0) || (status & SDIO_FLAG_CTIMEOUT))/* 超时处理 */
    {
        errorstatus = SD_CMD_RSP_TIMEOUT; /* 当前卡不是2.0兼容卡,或者不支持设定的电压范围 */
        SDIO_ClearFlag(SDIO_FLAG_CTIMEOUT);/* 清除命令响应超时标志 */
        return errorstatus;
    }

    if (status & SDIO_FLAG_CCRCFAIL)/* CRC错误处理 */
    {
        errorstatus = SD_CMD_CRC_FAIL;
        SDIO_ClearFlag(SDIO_FLAG_CCRCFAIL);
        return errorstatus;
    }

    if (status & SDIO_FLAG_CMDREND)/* 成功接收到响应 */
    {
        errorstatus = SD_OK;
        SDIO_ClearFlag(SDIO_FLAG_CMDREND);
    }

    return errorstatus;
}

/**
 * @brief       SDMMC 使能宽总线模式
 * @param       enx : 0, 不使能; 1, 使能;
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_wide_bus_enable(uint8_t enx)
{
    SD_Error errorstatus = SD_OK;
    uint32_t scr[2] = {0, 0};
    uint8_t arg = 0X00;

    if (enx) arg = 0X02;
    else arg = 0X00;

    if (SDIO->RESP1 & SD_CARD_LOCKED) return SD_LOCK_UNLOCK_FAILED;/* SD卡处于LOCKED状态 */

    errorstatus = sdmmc_find_scr(RCA, scr); /* 得到SCR寄存器数据 */
    if (errorstatus != SD_OK) return errorstatus;

    if ((scr[1] & SD_WIDE_BUS_SUPPORT) != SD_ALLZERO)/* 支持宽总线 */
    {
        sdmmc_send_cmd(SD_CMD_APP_CMD, 1, (uint32_t)RCA << 16); /* 发送CMD55+RCA,短响应 */
        errorstatus = sdmmc_cmd_resp1_error(SD_CMD_APP_CMD);

        if (errorstatus != SD_OK) return errorstatus;

        sdmmc_send_cmd(SD_CMD_APP_SD_SET_BUSWIDTH, 1, arg);/* 发送ACMD6,短响应,参数:10,4位;00,1位. */
        errorstatus = sdmmc_cmd_resp1_error(SD_CMD_APP_SD_SET_BUSWIDTH);
        return errorstatus;
    }
    else
    {
        return SD_REQUEST_NOT_APPLICABLE; /* 不支持宽总线设置 */
    }
}


/**
 * @brief   SDIO 检查卡是否正在执行写操作
 * @param   pstatus: 卡状态指针
 * @retval  SD错误状态
 */
static SD_Error sdmmc_is_card_programming(uint8_t *pstatus)
{
    volatile uint32_t respR1 = 0;
	
		sdmmc_send_cmd(SD_CMD_SEND_STATUS, 1, (uint32_t)RCA << 16);/* 发送CMD13 */
    /* 等待命令响应完成或错误发生 */
    /* bit0: CCRCFAIL, bit2: CTIMEOUT, bit6: CMDREND */
    while ((SDIO_GetFlagStatus(SDIO_FLAG_CCRCFAIL) == RESET) &&
           (SDIO_GetFlagStatus(SDIO_FLAG_CTIMEOUT) == RESET) &&
           (SDIO_GetFlagStatus(SDIO_FLAG_CMDREND)  == RESET));
    
    /* 检查命令CRC校验失败 */
    if (SDIO_GetFlagStatus(SDIO_FLAG_CCRCFAIL) != RESET)
    {
        SDIO_ClearFlag(SDIO_FLAG_CCRCFAIL);
        return SD_CMD_CRC_FAIL;
    }
    
    /* 检查命令响应超时 */
    if (SDIO_GetFlagStatus(SDIO_FLAG_CTIMEOUT) != RESET)
    {
        SDIO_ClearFlag(SDIO_FLAG_CTIMEOUT);
        return SD_CMD_RSP_TIMEOUT;
    }
    
    /* 检查响应命令索引是否正确 */
    if (SDIO_GetCommandResponse() != SD_CMD_SEND_STATUS)
    {
        return SD_ILLEGAL_CMD;
    }
    
    /* 清除所有SDIO标志位 */
    SDIO_ClearFlag(SDIO_FLAG_ALL);
    
    /* 获取响应数据，提取卡编程状态 (bits[12:9]) */
    respR1 = SDIO_GetResponse(SDIO_RESP1);
    *pstatus = (uint8_t)((respR1 >> 9) & 0x0000000F);
    
    return SD_OK;
}

/**
 * @brief       SDMMC 读取当前卡状态
 * @param       pstatus : 当前状态.
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_send_status(uint32_t *pstatus)
{
    SD_Error errorstatus = SD_OK;

    if (pstatus == NULL)
    {
        errorstatus = SD_INVALID_PARAMETER;
        return errorstatus;
    }

    sdmmc_send_cmd(SD_CMD_SEND_STATUS, 1, RCA << 16);/* 发送CMD13,短响应 */
    errorstatus = sdmmc_cmd_resp1_error(SD_CMD_SEND_STATUS);/* 查询响应状态 */

    if (errorstatus != SD_OK) return errorstatus;

    *pstatus = SDIO->RESP1; /* 读取响应值 */
    return errorstatus;
}

/**
 * @brief       返回SD卡的状态
 */
SDCardState sdmmc_get_status(void)
{
    uint32_t resp1 = 0;

    if (sdmmc_send_status(&resp1) != SD_OK)
    {
        return SD_CARD_ERROR;
    }
    else
    {
        return (SDCardState)((resp1 >> 9) & 0x0F);
    }
}

/**
 * @brief       SDIO 查找SD卡的SCR寄存器值
 * @param       rca  : 卡相对地址
 * @param       pscr : 数据缓存区(存储SCR内容)
 * @retval      0, 正常;  其他, 错误代码(详见SD_Error定义);
 */
static SD_Error sdmmc_find_scr(uint16_t rca, uint32_t *pscr)
{
    SD_Error errorstatus = SD_OK;
    uint32_t tempscr[2] = {0, 0};

    sdmmc_send_cmd(SD_CMD_SET_BLOCKLEN, 1, 8); /* 发送CMD16,短响应,设置Block Size为8字节 */
    errorstatus = sdmmc_cmd_resp1_error(SD_CMD_SET_BLOCKLEN);
    if (errorstatus != SD_OK) return errorstatus;

    sdmmc_send_cmd(SD_CMD_APP_CMD, 1, (uint32_t)rca << 16);/* 发送CMD55,短响应 */
    errorstatus = sdmmc_cmd_resp1_error(SD_CMD_APP_CMD);
    if (errorstatus != SD_OK) return errorstatus;

    sdmmc_send_data_cfg(SD_DATATIMEOUT, 8, 3, 1); /* 8个字节长度,block为8字节,SD卡到SDMMC. */
    sdmmc_send_cmd(SD_CMD_SD_APP_SEND_SCR, 1, 0);/* 发送ACMD51,短响应,参数为0 */
    errorstatus = sdmmc_cmd_resp1_error(SD_CMD_SD_APP_SEND_SCR);
    if (errorstatus != SD_OK) return errorstatus;

    while (!(SDIO->STA & (SDIO_STA_RXOVERR | SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT | SDIO_STA_DBCKEND | SDIO_STA_DATAEND)))
    {
        if (!(SDIO->STA & (1 << 19))) /* 接收FIFO数据可用 */
        {
            tempscr[0] = SDIO->FIFO; /* 读取FIFO内容 */
            tempscr[1] = SDIO->FIFO;/* 读取FIFO内容 */
            break;
        }
    }

    if (SDIO->STA & (1 << 3)) /* 接收数据超时 */
    {
        SDIO->ICR |= 1 << 3; /* 清除标记 */
        return SD_DATA_TIMEOUT;
    }
    else if (SDIO->STA & (1 << 1)) /* 已发送/接收的数据块CRC校验错误 */
    {
        SDIO->ICR |= 1 << 1; /* 清除标记 */
        return SD_DATA_CRC_FAIL;
    }
    else if (SDIO->STA & (1 << 5))/* 接收FIFO溢出 */
    {
        SDIO->ICR |= 1 << 5; /* 清除标记 */
        return SD_RX_OVERRUN;
    }

    SDIO->ICR = 0X1FE00FFF;/* 清除标记 */
		
		 /* 把数据顺序按8位为单位倒过来. */
    *(pscr + 1) = ((tempscr[0] & SD_0TO7BITS) << 24) | ((tempscr[0] & SD_8TO15BITS) << 8) | 
                  ((tempscr[0] & SD_16TO23BITS) >> 8) | ((tempscr[0] & SD_24TO31BITS) >> 24);
    *(pscr) = ((tempscr[1] & SD_0TO7BITS) << 24) | ((tempscr[1] & SD_8TO15BITS) << 8) | 
              ((tempscr[1] & SD_16TO23BITS) >> 8) | ((tempscr[1] & SD_24TO31BITS) >> 24);

    return errorstatus;
}

/**
 * @brief       SDIO中断处理函数
 * @param       无
 * @retval      无
 */
void SDIO_IRQHandler(void)
{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    /* 数据传输完成 */
    if (SDIO_GetITStatus(SDIO_IT_DATAEND) != RESET)
    {
        /* 多块传输需要发送停止命令 */
        if (g_sdio_nblks > 1)
        {
            if ((STD_CAPACITY_SD_CARD_V1_1 == CardType) || 
                (STD_CAPACITY_SD_CARD_V2_0 == CardType) || 
                (HIGH_CAPACITY_SD_CARD == CardType))
            {
                sdmmc_send_cmd(SD_CMD_STOP_TRANSMISSION, 1, 0);
                sdmmc_cmd_resp1_error(SD_CMD_STOP_TRANSMISSION);
            }
        }
        
        g_sdio_transfer_end = 1;
        SDIO_ClearITPendingBit(SDIO_IT_DATAEND);
				
				xSemaphoreGiveFromISR(g_sdio_transfer_sem, &xHigherPriorityTaskWoken);//释放信号量 
    }
    
    /* 数据超时错误 */
    if (SDIO_GetITStatus(SDIO_IT_DTIMEOUT) != RESET)
    {
        g_sdio_transfer_error = 1;
        g_sdio_error = SD_DATA_TIMEOUT;
        SDIO_ClearITPendingBit(SDIO_IT_DTIMEOUT);
			
				xSemaphoreGiveFromISR(g_sdio_transfer_sem, &xHigherPriorityTaskWoken);//释放信号量 
    }
    
    /* 数据CRC错误 */
    if (SDIO_GetITStatus(SDIO_IT_DCRCFAIL) != RESET)
    {
        g_sdio_transfer_error = 1;
        g_sdio_error = SD_DATA_CRC_FAIL;
        
        /* CRC错误时，多块传输需要发送停止命令 */
        if (g_sdio_nblks > 1)
        {
            sdmmc_send_cmd(SD_CMD_STOP_TRANSMISSION, 1, 0);
            sdmmc_cmd_resp1_error(SD_CMD_STOP_TRANSMISSION);
        }
        
        SDIO_ClearITPendingBit(SDIO_IT_DCRCFAIL);
				
				xSemaphoreGiveFromISR(g_sdio_transfer_sem, &xHigherPriorityTaskWoken);//释放信号量
    }
    
    /* 接收FIFO溢出错误 */
    if (SDIO_GetITStatus(SDIO_IT_RXOVERR) != RESET)
    {
        g_sdio_transfer_error = 1;
        g_sdio_error = SD_RX_OVERRUN;
        SDIO_ClearITPendingBit(SDIO_IT_RXOVERR);
			
				xSemaphoreGiveFromISR(g_sdio_transfer_sem, &xHigherPriorityTaskWoken);//释放信号量
    }
    
    /* 发送FIFO欠载错误 */
    if (SDIO_GetITStatus(SDIO_IT_TXUNDERR) != RESET)
    {
        g_sdio_transfer_error = 1;
        g_sdio_error = SD_TX_UNDERRUN;
        SDIO_ClearITPendingBit(SDIO_IT_TXUNDERR);
			
				xSemaphoreGiveFromISR(g_sdio_transfer_sem, &xHigherPriorityTaskWoken);//释放信号量
    }
		
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);//如果需要，触发任务切换
}

/**
 * @brief       DMA2 Stream3中断处理函数（SDIO DMA）
 * @param       无
 * @retval      无
 */
void DMA2_Stream3_IRQHandler(void)
{
    /* DMA传输完成 */
    if (DMA_GetITStatus(SD_SDIO_DMA_STREAM, DMA_IT_TCIF3) != RESET)
    {
        DMA_ClearITPendingBit(SD_SDIO_DMA_STREAM, DMA_IT_TCIF3);
        /* DMA完成不代表SDIO完成，由SDIO中断处理 */
    }
    
    /* DMA传输错误 */
    if (DMA_GetITStatus(SD_SDIO_DMA_STREAM, DMA_IT_TEIF3) != RESET)
    {
        g_sdio_transfer_error = 1;
        g_sdio_error = SD_ERROR;
        DMA_ClearITPendingBit(SD_SDIO_DMA_STREAM, DMA_IT_TEIF3);
    }
}

/**
 * @brief       读SD卡(fatfs/usb调用)
 * @param       pbuf  : 数据缓存区
 * @param       saddr : 扇区地址
 * @param       cnt   : 扇区个数
 * @retval      0, 正常;  其他, 错误代码
 */
uint8_t sd_read_disk(uint8_t *pbuf, uint32_t saddr, uint32_t cnt)
{
    uint8_t sta = SD_OK;
    long long lsaddr = saddr;
	
		/* 获取互斥锁 */
		if (xSemaphoreTake(g_sdio_mutex, pdMS_TO_TICKS(SDIO_TRANSFER_TIMEOUT_MS)) != pdTRUE)
			{
					return SD_ERROR;
			}

    if (CardType != STD_CAPACITY_SD_CARD_V1_1) lsaddr <<= 9;

    sta = sdmmc_read_blocks(pbuf, lsaddr, 512, cnt);
			
		xSemaphoreGive(g_sdio_mutex);/* 释放互斥锁 */

    return sta;
}

/**
 * @brief       写SD卡(fatfs/usb调用)
 * @param       pbuf  : 数据缓存区
 * @param       saddr : 扇区地址
 * @param       cnt   : 扇区个数
 * @retval      0, 正常;  其他, 错误代码
 */
/*互斥锁作用：SD 卡是串行设备——同一时刻只能有一个传输在进行（SDIO 硬件状态机、DMA 流都是单份的）。而系统里至少有两个任务会访问它：
  SDCard_WriteTask：写日志 CSV 文件（经 FatFs）
  Ymodem_Task：读日志文件导出（经 FatFs）*/
uint8_t sd_write_disk(uint8_t *pbuf, uint32_t saddr, uint32_t cnt)
{
    uint8_t sta = SD_OK;
    long long lsaddr = saddr;
	
		/* 获取互斥锁 */
		if (xSemaphoreTake(g_sdio_mutex, pdMS_TO_TICKS(SDIO_TRANSFER_TIMEOUT_MS)) != pdTRUE)
			{
					return SD_ERROR;
			}
		
		//高容量SD卡（SDHC/SDXC，≥4GB）使用字节地址而非扇区地址，需要左移9位（乘以512）
    if (CardType != STD_CAPACITY_SD_CARD_V1_1) lsaddr <<= 9;

    sta = sdmmc_write_blocks(pbuf, lsaddr, 512, cnt);/* 读取单个/多个sector */
			
		xSemaphoreGive(g_sdio_mutex);/* 释放互斥锁 */

    return sta;
}

