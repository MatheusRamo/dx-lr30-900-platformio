#include "main.h"
#include "driver_usart.h"
/*
 * 定义全局变量
*/
UART_HandleTypeDef husart;

/*
 *  函数名：void UsartInit(uint32_t baudrate)
 *  输入参数：baudrate-串口波特率
 *  输出参数：无
 *  返回值：无
 *  函数作用：初始化USART的波特率，收发选择，有效数据位等
*/
void UsartInit(uint32_t baudrate)
{
    husart.Instance         = USARTx;                  // 选择USART1
    husart.Init.BaudRate    = baudrate;                // 配置波特率
    husart.Init.WordLength  = USART_WORDLENGTH_8B;     // 配置数据有效位为8bit
    husart.Init.StopBits    = USART_STOPBITS_1;        // 配置一位停止位
    husart.Init.Parity      = USART_PARITY_NONE;       // 不设校验位
    husart.Init.Mode        = USART_MODE_TX_RX;        // 可收可发
    husart.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
    
    // 使用库函数初始化USART1的参数
    if (HAL_UART_Init(&husart) != HAL_OK)
    {
        Error_Handler();
    }
    
    HAL_UART_Receive_IT(&husart,&pdata, sizeof(pdata));
}

/*
 *  函数名：void HAL_USART_MspInit(USART_HandleTypeDef* husart)
 *  输入参数：husart-USART句柄
 *  输出参数：无
 *  返回值：无
 *  函数作用：使能USART1的时钟，使能引脚时钟，并配置引脚的复用功能
*/
void HAL_UART_MspInit(UART_HandleTypeDef* husart)
{
    // 定义GPIO结构体对象
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(husart->Instance==USARTx)
    {
        // 使能USART1的时钟
        USARTx_CLK_EN();
        
        // 使能USART1的输入输出引脚的时钟
        USARTx_GPIO_CLK_EN();
        
        /**USART1 GPIO Configuration   
        PA9      ------> USART1_TX
        PA10     ------> USART1_RX
        */
        GPIO_InitStruct.Pin = USARTx_TX_PIN;            // 选择USART1的TX引脚
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;         // 配置为复用推挽功能
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;   // 引脚翻转速率快
        HAL_GPIO_Init(USARTx_PORT, &GPIO_InitStruct);   // 初始化TX引脚

        GPIO_InitStruct.Pin = USARTx_RX_PIN;            // 选择RX引脚
        GPIO_InitStruct.Mode = GPIO_MODE_AF_INPUT;      // 配置为输入
        HAL_GPIO_Init(USARTx_PORT, &GPIO_InitStruct);   // 初始化RX引脚

        /*抢占优先级0，子优先级1*/
        HAL_NVIC_SetPriority(USART1_IRQn ,0,1);
        HAL_NVIC_EnableIRQ(USART1_IRQn); /*使能USART1中断通道*/
        
    }
}

/*
 *  函数名：void HAL_USART_MspDeInit(USART_HandleTypeDef* husart)
 *  输入参数：husart-USART句柄
 *  输出参数：无
 *  返回值：无
 *  函数作用：失能USART1的时钟，失能引脚时钟，并将RX/TX引脚配置为默认状态
*/
void HAL_UART_MspDeInit(UART_HandleTypeDef* husart)
{
    if(husart->Instance==USARTx)
    {
        USARTx_CLK_DIS();

        /**USART1 GPIO Configuration    
        PA9      ------> USART1_TX
        PA10     ------> USART1_RX
        */
        HAL_GPIO_DeInit(USARTx_PORT, USARTx_TX_PIN | USARTx_RX_PIN);
    }
} 


void Usart_SendString(uint8_t *str)
{
    unsigned int k=0;
    do {
        HAL_UART_Transmit(&husart,(uint8_t *)(str + k) ,1,1000);
        k++;
    } while (*(str + k)!='\0');
}




/*****************************************************
*function:	写字符文件函数
*param1:	输出的字符
*param2:	文件指针
*return:	输出字符的ASCII码
******************************************************/
int fputc(int ch, FILE *f)
{
	HAL_UART_Transmit(&husart, (uint8_t*)&ch, 1, 10);
	return ch;
}

/*****************************************************
*function:	读字符文件函数
*param1:	文件指针
*return:	读取字符的ASCII码
******************************************************/
int fgetc(FILE *f)
{
    uint8_t ch = 0;
    HAL_UART_Receive(&husart, (uint8_t*)&ch, 1, 10);
    return (int)ch;
}


/*
 * 添加如下代码，则不需要在工程设置中勾选Use MicroLIB
*/

#pragma import(__use_no_semihosting)
 
struct __FILE
{
	int a;
};
 
FILE __stdout;
FILE __stdin;
void _sys_exit(int x)
{
	
}
