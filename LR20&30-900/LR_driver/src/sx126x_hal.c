#include <sx126x_hal.h>
#include "driver_usart.h"
#include "driver_spi.h"
#include "driver_key.h"
#include "driver_timer.h"
#include "UserConfig.h"
#include "stm32f1xx_hal_gpio.h"
#include "sx126x.h"






sx126x_hal_status_t sx126x_hal_write(const void *context, const uint8_t *command, const uint16_t command_length,const uint8_t *data, const uint16_t data_length)
{
	  while(sx126x_get_busy())
	  {
	  }
	  HAL_GPIO_WritePin(LCC68_NSS_PORT, LCC68_NSS_PIN, GPIO_PIN_RESET);
      if(command_length > 0)
      {
        SPI_WriteBytes((uint8_t *)command,command_length);
      }

      while(sx126x_get_busy())
      {
      }
      
      if(data_length > 0)
      {
        SPI_WriteBytes((uint8_t *)data,data_length);
      }
	  HAL_GPIO_WritePin(LCC68_NSS_PORT, LCC68_NSS_PIN, GPIO_PIN_SET);
	  return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *command, const uint16_t command_length,uint8_t *data, const uint16_t data_length)
{          
	  while(sx126x_get_busy())
	  {
	  }
	  HAL_GPIO_WritePin(LCC68_NSS_PORT, LCC68_NSS_PIN, GPIO_PIN_RESET);
      
      if(command_length > 0)
      {
        SPI_WriteBytes((uint8_t *)command,command_length);
      }
      while(sx126x_get_busy())
      {
      }
      
      if(data_length > 0)
      {
	    SPI_ReadBytes((uint8_t *)data, data_length);
      }
	  HAL_GPIO_WritePin(LCC68_NSS_PORT, LCC68_NSS_PIN, GPIO_PIN_SET);
	  return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_reset(const void *context)
{
    while(sx126x_get_busy())
    {
    }
    ms_timer_delay(20);
	HAL_GPIO_WritePin(LCC68_NRST_PORT, LCC68_NRST_PIN, GPIO_PIN_RESET);
    ms_timer_delay(50);
	HAL_GPIO_WritePin(LCC68_NRST_PORT, LCC68_NRST_PIN, GPIO_PIN_SET);
    ms_timer_delay(20);
    while(sx126x_get_busy())
    {
    }
	return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_wakeup(const void* context)
{
      while(sx126x_get_busy())
      {
      }
      uint8_t data[] = {0xc0,0x00};
	  HAL_GPIO_WritePin(LCC68_NSS_PORT, LCC68_NSS_PIN, GPIO_PIN_RESET);
	  // 向0xC0寄存器写入0x00 （用户实现）
      SPI_WriteBytes(data,2);
	  HAL_GPIO_WritePin(LCC68_NSS_PORT, LCC68_NSS_PIN, GPIO_PIN_SET);
	  return SX126X_HAL_STATUS_OK;
}

int sx126x_get_busy(void)
{
	return HAL_GPIO_ReadPin(LCC68_BUSY_PORT,LCC68_BUSY_PIN);
}
