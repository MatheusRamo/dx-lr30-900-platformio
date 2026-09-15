#ifndef __DRIVER_USART_H
#define __DRIVER_USART_H

#include <stdio.h>
#include "stm32f1xx_hal.h"
#include "UserConfig.h"









/*********************
 * 引脚宏定义
**********************/
#define USARTx                  USART1
#define USARTx_TX_PIN           LOG_UART_TX_PIN
#define USARTx_RX_PIN           LOG_UART_RX_PIN
#define USARTx_PORT             GPIOA
#define USARTx_GPIO_CLK_EN()    __HAL_RCC_GPIOA_CLK_ENABLE()
#define USARTx_CLK_EN()         __HAL_RCC_USART1_CLK_ENABLE()
#define USARTx_CLK_DIS()        __HAL_RCC_USART1_CLK_DISABLE()

/*********************
 * 函数宏定义
**********************/

/*********************
 * 全局变量声明
**********************/
extern UART_HandleTypeDef husart;

/*********************
 * 对外函数API
**********************/

/*
 *  函数名：void UsartInit(uint32_t baudrate)
 *  输入参数：baudrate-串口波特率
 *  输出参数：无
 *  返回值：无
 *  函数作用：初始化USART的波特率，收发选择，有效数据位等
*/
extern void UsartInit(uint32_t baudrate);

extern void Usart_SendString(uint8_t *str);



#endif

