#include "main.h"
#include "driver_spi.h"

/*
 * 定义全局变量
*/
SPI_HandleTypeDef hspi;

/*
 *  函数名：void SPI_Init(void)
 *  输入参数：
 *  输出参数：无
 *  返回值：无
 *  函数作用：初始化SPI
*/
void SPI_Init(void)
{
  hspi.Instance               = SPIx;
  hspi.Init.Mode              = SPI_MODE_MASTER;	        // 设置SPI模式(主机模式)
  hspi.Init.Direction         = SPI_DIRECTION_2LINES;       // 设置SPI工作方式(全双工)
  
  hspi.Init.DataSize          = SPI_DATASIZE_8BIT;          // 设置数据格式(8bit长度)
  
  hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;           // 设置时钟极性(CPOL=0)
  
  hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;            // 设置时钟相位(CPHA=0)
  
  hspi.Init.NSS               = SPI_NSS_SOFT;	            // 设置片选方式(软件片选,自定义GPIO)
  hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;   // 设置SPI时钟波特率分频(16分频,SPI1:72/16=4.5MHz)
  hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;           // 设置大小端模式(MSB,高位在前)
  hspi.Init.TIMode            = SPI_TIMODE_DISABLE;         // 设置帧格式(关闭TI模式)
  hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE; // 设置CRC校验(关闭CRC校验)
  hspi.Init.CRCPolynomial     = 7;                          // 设置CRC校验多项式(范围:1~65535)

  if(HAL_SPI_Init(&hspi) != HAL_OK)
  {
      Error_Handler();
  }      
  
  __HAL_SPI_ENABLE(&hspi); 
}


/*
 *  函数名：void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
 *  输入参数：hspi-SPI句柄
 *  输出参数：无
 *  返回值：无
 *  函数作用：使能SPI的时钟，使能引脚时钟，并配置引脚的复用功能
*/
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef  GPIO_InitStruct;

    if(hspi->Instance==SPIx)
    {
        // SPI相关引脚时钟使能
		ICM_CS_GPIO_CLK_ENABLE();
		SPIx_SCK_GPIO_CLK_ENABLE(); 
		SPIx_MISO_GPIO_CLK_ENABLE();
		SPIx_MOSI_GPIO_CLK_ENABLE();
		
		SPIx_CLK_ENABLE(); // SPI时钟使能

		
        // SPI软件片选引脚输出
		GPIO_InitStruct.Pin       = ICM_CS_PIN;
		GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;      
		HAL_GPIO_Init(ICM_CS_GPIO_PORT, &GPIO_InitStruct);
		ICM_CS(1);      // CS初始化高


        //SCL
        GPIO_InitStruct.Pin       = SPIx_SCK_PIN;
		GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH; 
		HAL_GPIO_Init(SPIx_SCK_GPIO_PORT, &GPIO_InitStruct);

        //MISO
	    GPIO_InitStruct.Pin       = SPIx_MISO_PIN;
		GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH; 
		HAL_GPIO_Init(SPIx_MISO_GPIO_PORT, &GPIO_InitStruct);

        //MOSI
		GPIO_InitStruct.Pin       = SPIx_MOSI_PIN;
		GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
		GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH; 
		HAL_GPIO_Init(SPIx_MOSI_GPIO_PORT, &GPIO_InitStruct);  
	}
}

/*
 *  函数名：void SPI_WriteBytes(uint8_t *pdata, uint16_t sz)
 *  输入参数：pdata -> 要写的数据指针; sz->要写的字节个数
 *  输出参数：无
 *  返回值：无
 *  函数作用：SPI发送一个字节
*/
void SPI_WriteBytes(uint8_t *pdata, uint16_t sz)
{
    HAL_SPI_Transmit(&hspi, pdata, sz, 20);
}

/*
 *  函数名：uint8_t SPI_RWOneByte(uint8_t pdata)
 *  输入参数：pdata -> 要写的数据
 *  输出参数：
 *  返回值：读到的数据
 *  函数作用：SPI读写一个字节
*/
uint8_t SPI_RWOneByte(uint8_t pdata)
{
    uint8_t temp = 0;
    
    HAL_SPI_TransmitReceive(&hspi, &pdata, &temp, 1, 20);
    
    return temp;
}

/*
 *  函数名：void SPI_ReadBytes(uint8_t *pdata, uint16_t sz)
 *  输入参数：pdata -> 要读的数据指针; sz -> 要读的数据个数
 *  输出参数：
 *  返回值：
 *  函数作用：SPI读N个字节
*/
void SPI_ReadBytes(uint8_t *pdata, uint16_t sz)
{
    HAL_SPI_Receive(&hspi, pdata, sz, 20);
}
