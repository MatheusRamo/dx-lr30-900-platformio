#ifndef __DRIVER_TIMER_H
#define __DRIVER_TIMER_H

#include "stm32f1xx_hal.h"

/*
 *  函数名：void TimerInit(void)
 *  输入参数：
 *  输出参数：无
 *  返回值：无
 *  函数作用：初始化定时器，使其时钟频率为4MHz
*/
extern void TimerInit(void);
extern void TIM3_Init(void);

/*
 *  函数名：void us_timer_delay(uint16_t t)
 *  输入参数：t-延时时间us
 *  输出参数：无
 *  返回值：无
 *  函数作用：定时器实现的延时函数，延时时间为t us，为了缩短时间，函数体使用寄存器操作，用户可对照手册查看每个寄存器每一位的意义
*/
extern void us_timer_delay(uint16_t t);




extern void ms_timer_delay(uint16_t t);

extern TIM_HandleTypeDef htim;
extern TIM_HandleTypeDef htim3;

#endif
