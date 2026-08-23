#include <stdio.h>

#include "main.h"
#include "driver_usart.h"
#include "driver_spi.h"
#include "driver_key.h"
#include "driver_timer.h"
#include "UserConfig.h"
#include "sx126x.h"

uint8_t DataLen = 0;
uint8_t pdata = 0;
uint8_t rxbuff[SIZE_DATA] = {0};

void UartTimeroutStart(void)
{

  __HAL_TIM_CLEAR_IT(&htim3, TIM_IT_UPDATE);      
  HAL_TIM_Base_Start_IT(&htim3);

}
void UartTimeroutStop(void)
{
//    __HAL_TIM_CLEAR_IT(&htim3, TIM_IT_UPDATE);      
    HAL_TIM_Base_Stop_IT(&htim3);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    __disable_irq();  // 添加临界区保护  
    UartTimeroutStop();
    
    rxbuff[DataLen++] = pdata;
    if(DataLen == SIZE_DATA)
    {
        queueEnqueue(pUart1RxQueue, &rxbuff, DataLen);
        memset(rxbuff,0,SIZE_DATA);
        DataLen = 0;
    }
    UartTimeroutStart();
    HAL_UART_Receive_IT(&husart,&pdata, sizeof(pdata));

    __enable_irq();  // 恢复中断
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef * htim)
{

    if(htim->Instance == TIM3)
    {
        if(0 != DataLen )
        {
            queueEnqueue(pUart1RxQueue, &rxbuff, DataLen);
            memset(rxbuff,0,SIZE_DATA);
            DataLen = 0;
        }
    }
   UartTimeroutStop();
}






int main(void)
{    
    // 初始化HAL库函数必须要调用此函数
    HAL_Init();

    /*
     * 系统时钟即AHB/APB时钟配置    
	 * 使用外部高速时钟HSE（8MHz）配置系统时钟，经过PLL放大9倍，得到72MHz
    */
    SystemClock_Config();

    //初始化接收队列
    UartRxQueue.front = 0;
    UartRxQueue.tail = 0;
    UartRxQueue.count = 0;
    pUart1RxQueue = (pQueue)&UartRxQueue;
  
    // 初始化USART1，设置波特率为115200 bps
    UsartInit(9600); 
    
    // 初始化定时器
    TimerInit();    
    TIM3_Init();
    // 初始化SPI
    SPI_Init();
    gpio_init();
  
    //Lora初始化
    LoraInit();
    DIO1_Init();
    printf("**********************************************\n\r");
    printf("-->Power On\n\r");
		printf("-->V1.2.35\n\r");
    printf("-->DX SX1262(LLCC68) TEST :%d %d\n\r",TEST,LORA_FRE);
    printf("**********************************************\n\r");
    ms_timer_delay(1000);    

    IrqFired = false;
    radioFlag = 0x00;
    
#if TEST
        sx126x_set_tx_cw(NULL);
#endif
    for(;;)
    {   
	    Data_Processing();
        DX_Lora_RadioIrqProcess();
    }
}

/*
 *  函数名：void Error_Handler(void)
 *  输入参数：无
 *  输出参数：无
 *  返回值：无
*  函数作用：程序错误处理函数，此处暂时设为死循环，不做任何动作
*/
void Error_Handler(void)
{
    while(1)
    {
        printf("%s, %d\n", __FUNCTION__, __LINE__); 
    }
}
