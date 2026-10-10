#include "power_mgmt.h"
#include <stdio.h> 
#include "lcd.h"



// ADC初始化 - 配置VBAT通道
void adc_vbat_init(void)
{
		ADC_InitTypeDef ADC_InitStructure;
		ADC_CommonInitTypeDef ADC_CommonInitStructure;

		// 使能ADC1时钟
		RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

		// ADC公共配置
		ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
		ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div4;
		ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
		ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
		ADC_CommonInit(&ADC_CommonInitStructure);

		// ADC1配置
		ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
		ADC_InitStructure.ADC_ScanConvMode = DISABLE;
		ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
		ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
		ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
		ADC_InitStructure.ADC_NbrOfConversion = 1;
		ADC_Init(ADC1, &ADC_InitStructure);

		// 使能VBAT通道
		ADC_VBATCmd(ENABLE);

		// 使能ADC1
		ADC_Cmd(ADC1, ENABLE);
}

// 读取VBAT电压 (返回单位: mV)
uint16_t adc_read_vbat(void)
{
		uint16_t adc_value;
		uint16_t vbat_mv;

		// 配置通道18 (VBAT通道), 采样时间480周期
		ADC_RegularChannelConfig(ADC1, ADC_Channel_18, 1, ADC_SampleTime_480Cycles);

		// 启动转换
		ADC_SoftwareStartConv(ADC1);

		// 等待转换完成
		while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

		// 读取ADC值
		adc_value = ADC_GetConversionValue(ADC1);

		// 转换为电压值 (mV)
		// VBAT通道内部2分频，参考电压3.3V，12位ADC
		// 实际VBAT = (adc_value / 4095) * 3.3V * 2
		vbat_mv = (uint16_t)((adc_value * 3300 * 2) / 4095);

		return vbat_mv;
}

// 进入低功耗模式
void enter_low_power_mode(void)
{
		// 关闭LCD背光
		lcd_backlight_off();
		lcd_display_off();
		// 使能WKUP引脚唤醒功能
		PWR_WakeUpPinCmd(ENABLE);

		// 必须清除标志，否则无法进入Standby
		PWR_ClearFlag(PWR_FLAG_WU | PWR_FLAG_SB);
		// 进入Stop模式 (保留SRAM和寄存器，可被中断唤醒)
//		PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);

	// 或者进入Standby模式 (最低功耗，只能通过WKUP引脚或RTC唤醒)
		PWR_EnterSTANDBYMode();
}
