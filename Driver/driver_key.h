#ifndef __DRIVER_KEY_H
#define __DRIVER_KEY_H

#include "stm32f1xx_hal.h"

/*********************
 * 引脚宏定义
**********************/


#define KEY_UP_GPIO_PIN               LCC68_DIO1_PIN
#define KEY_UP_GPIO_PORT              LCC68_DIO1_PORT
#define KEY_UP_GPIO_CLK_EN()          __HAL_RCC_GPIOC_CLK_ENABLE()



/*********************
 * 函数宏定义
**********************/
/*
 * 按键状态读取函数宏定义
*/
#define READ_DIO1                  HAL_GPIO_ReadPin(KEY_UP_GPIO_PORT, KEY_UP_GPIO_PIN)


/*
 *  函数名：void KeyInit(void)
 *  输入参数：无
 *  输出参数：无
 *  返回值：无
 *  函数作用：初始化按键的引脚，配置为下降沿触发外部中断
*/
extern void DIO1_Init(void);


#endif
