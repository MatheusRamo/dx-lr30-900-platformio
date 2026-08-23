



#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "main.h"
#include "driver_usart.h"
#include "driver_spi.h"
#include "driver_key.h"
#include "driver_timer.h"
#include "UserConfig.h"
#include "sx126x.h"
#include "sx126x_hal.h"
#include <string.h>
#include <stdlib.h>


static volatile uint8_t g_lora_test_rxs;
static volatile uint8_t g_lora_tx_done;


uint8_t IrqFired = false;
sx126x_rx_buffer_status_t offset = {0};
sx126x_pkt_status_lora_t RadioPktStatus;
sx126x_irq_mask_t radioFlag = 0;

static volatile RadioOperatingModes_t OperatingMode;
void LoraOpenRXMode(uint8_t Timerout);



void gpio_init(void)
{
    
    // 定义GPIO的结构体变量
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    // 使能LED的GPIO对应的时钟
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin =  LCC68_BUSY_PIN;       
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT; 
    GPIO_InitStruct.Pull = GPIO_PULLUP;        
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    
    // 初始化引脚配置
    HAL_GPIO_Init(LCC68_BUSY_PORT, &GPIO_InitStruct);
    ms_timer_delay(20);


    GPIO_InitStruct.Pin =  LCC68_NRST_PIN | LCC68_RXEN_PIN | LCC68_TXEN_PIN;       // 选择LED的引脚
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; // 设置为推挽输出模式
    GPIO_InitStruct.Pull = GPIO_PULLUP;         // 默认上拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;// 引脚输出速度设置为慢

    // 初始化引脚配置
    HAL_GPIO_Init(LCC68_NRST_PORT, &GPIO_InitStruct);
	HAL_GPIO_WritePin(LCC68_NRST_PORT, LCC68_NRST_PIN, GPIO_PIN_SET);
	HAL_GPIO_WritePin(LCC68_NRST_PORT, LCC68_RXEN_PIN, GPIO_PIN_SET);
	HAL_GPIO_WritePin(LCC68_NRST_PORT, LCC68_TXEN_PIN, GPIO_PIN_SET);

    ms_timer_delay(20);
}


void SetTxHz(uint16_t HZ)
{
    sx126x_set_rf_freq(NULL,HZ * 1000000);
	
}




RadioOperatingModes_t sx1262GetOperatingMode(void)
{
    return OperatingMode;
}

void sx1262SetOperatingMode(RadioOperatingModes_t mode)
{
    OperatingMode = mode;
}



void RxEn(void)
{

    HAL_GPIO_WritePin(LCC68_RXEN_PORT, LCC68_RXEN_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCC68_TXEN_PORT, LCC68_TXEN_PIN, GPIO_PIN_RESET);

}
void TxEn(void)
{

    HAL_GPIO_WritePin(LCC68_RXEN_PORT, LCC68_RXEN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCC68_TXEN_PORT, LCC68_TXEN_PIN, GPIO_PIN_SET);

}




void LoraInit(void)
{
    g_lora_test_rxs = false;
    g_lora_tx_done = true;
    
    /* IO复位+CS唤醒 模块*/
    sx126x_reset(NULL);
    sx126x_wakeup(NULL); 

    /* 状态机设定 */
    /* 进入 STDBY_RC 待机配置模式 */
    sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC );
    sx126x_set_standby(NULL, SX126X_STANDBY_CFG_XOSC );
	
    /* 选择内部电压调节器模式 高效DC-DC */
    sx126x_set_reg_mode(NULL, SX126X_REG_MODE_DCDC);
	
	   /* 内部FIFO读写地址复位 0x00 */
    sx126x_set_buffer_base_address(NULL,0x00,0x00);
	
	  sx126x_set_pkt_type(NULL,SX126X_PKT_TYPE_LORA);  
		sx126x_set_trimming_capacitor_values(NULL,0x4,0x2f);

    sx126x_mod_params_lora_t params;
    params.bw = SX126X_LORA_BW_125;
    params.sf = SX126X_LORA_SF9;
    params.cr = SX126X_LORA_CR_4_6;
    params.ldro = 0x00;
    sx126x_set_lora_mod_params(NULL, &params);

    sx126x_pkt_params_lora_t params2;
    params2.crc_is_on = 0;
    params2.invert_iq_is_on = 0;
    params2.pld_len_in_bytes = 0xff;
    params2.header_type = SX126X_LORA_PKT_EXPLICIT;
    params2.preamble_len_in_symb = LORA_PREAMBLE_LENGTH;
    sx126x_set_lora_pkt_params(NULL, &params2);


    sx126x_pa_cfg_params_t  params3;
    params3.pa_duty_cycle = 0x04;
    params3.hp_max = 0x07;
    params3.device_sel = 0x00;
    params3.pa_lut = 0x01;
    sx126x_set_pa_cfg(NULL, &params3);
    		
    //打开dio1的中断 中断触发 SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE
    sx126x_set_dio_irq_params(NULL, SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE,SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL);
	
	 sx126x_set_tx_params(NULL,22 ,SX126X_RAMP_3400_US);   	
	  /* 设置载波频率(频点) */
   sx126x_set_rf_freq(NULL,LORA_FRE);
	 #if TEST
        g_lora_test_rxs = true;
        TxEn();
    #else
        LoraOpenRXMode(LORA_SX126x_SYMBOL_TIMEOUT);
    #endif
}


void set_LoraPacketParams(uint8_t size)
{

    sx126x_pkt_params_lora_t params2;
    params2.crc_is_on = 0;
    params2.invert_iq_is_on = 0;
    params2.pld_len_in_bytes = size;
    params2.header_type = SX126X_LORA_PKT_EXPLICIT;
    params2.preamble_len_in_symb = LORA_PREAMBLE_LENGTH;
    sx126x_set_lora_pkt_params(NULL, &params2);
}


void LoraDataSend(uint8_t *data,uint8_t len)
{
   TxEn();
   set_LoraPacketParams(len);
   sx126x_write_buffer(NULL, 0x00, data, len);
   sx126x_set_tx(NULL,6000);
   g_lora_tx_done = false;
   g_lora_test_rxs = false;
   sx1262SetOperatingMode(MODE_TX);
}




void LoraOpenRXMode(uint8_t Timerout)
{
     g_lora_test_rxs = true;
     sx126x_set_rx(NULL,Timerout);
     sx1262SetOperatingMode(MODE_RX);
     RxEn();

}



//以下为接收，发送处理
void OnTxDone(void)
{    
    g_lora_tx_done = true;
    g_lora_test_rxs = true;

    LoraOpenRXMode(LORA_SX126x_SYMBOL_TIMEOUT);
}

// void OnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr)
// {
// 		static uint8_t num = 0;
//     g_lora_tx_done = true;
//     g_lora_test_rxs = true;
// 		num += 1;
// 	  printf("%s",payload);
//     LoraOpenRXMode(LORA_SX126x_SYMBOL_TIMEOUT);

// }

//Minha modificacao

void OnRxDone(
    uint8_t* payload,
    uint16_t size,
    int16_t rssi,
    int8_t snr
)
{
    g_lora_tx_done = true;
    g_lora_test_rxs = true;

    printf("RX|RSSI=%d|SNR=%d|DATA=", rssi, snr);

    for(uint16_t i = 0; i < size; i++)
    {
        printf("%c", payload[i]);
    }

    printf("\r\n");

    LoraOpenRXMode(
        LORA_SX126x_SYMBOL_TIMEOUT
    );
}



void RxError(void)
{

    g_lora_tx_done = true;
    g_lora_test_rxs = true;




}

void CadDone ( bool channelActivityDetected )
{

    g_lora_test_rxs = true;
    g_lora_tx_done = true;


}

void RxTimeout(void)
{
    g_lora_tx_done = true;
    g_lora_test_rxs = true;   
    LoraOpenRXMode(LORA_SX126x_SYMBOL_TIMEOUT);    
}

void TxTimeout(void)
{
    g_lora_tx_done = true;
    g_lora_test_rxs = true;   
    LoraOpenRXMode(LORA_SX126x_SYMBOL_TIMEOUT);    
}


uint8_t radioRxbuff[255] = {0};
void Hz_set(char *data,uint8_t len)
{
	uint32_t num = strtol(data, NULL, 10);

	printf("==>%lu\r\n", (unsigned long)num * 1000000UL);
	SetTxHz(num);
}

void Data_Processing(void)
{

	if(!queueIsEmpty(pUart1RxQueue)&& g_lora_tx_done == true && g_lora_test_rxs == true)
    {
        #if !TEST
       uint8_t mydata[SIZE_DATA] = {0};
       uint8_t len = queueDequeue(pUart1RxQueue, &mydata);
       //发送数据
       LoraDataSend(&mydata[0],  len);
        #else
        uint8_t mydata[SIZE_DATA] = {0};
        uint8_t len = queueDequeue(pUart1RxQueue, &mydata);               
        Hz_set((char *)mydata,len);
       #endif       
     }
}





void DX_Lora_RadioIrqProcess(void)
{

    if(IrqFired == true)
    {
        __disable_irq();
        IrqFired = false;
        __enable_irq();

        if( ( radioFlag & SX126X_IRQ_TX_DONE ) == SX126X_IRQ_TX_DONE )
        {
            sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC );
            OnTxDone();
        }
        if( ( radioFlag & SX126X_IRQ_RX_DONE ) ==  SX126X_IRQ_RX_DONE )
        {
            sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC );
            sx126x_get_rx_buffer_status(NULL, &offset);
            sx126x_read_buffer(NULL, offset.buffer_start_pointer, radioRxbuff, offset.pld_len_in_bytes);
            sx126x_get_lora_pkt_status(NULL, &RadioPktStatus);
            OnRxDone(&radioRxbuff[0],  offset.pld_len_in_bytes, RadioPktStatus.rssi_pkt_in_dbm, RadioPktStatus.snr_pkt_in_db);
            memset(radioRxbuff,0,255);
         
        }

         if( ( radioFlag &  SX126X_IRQ_CRC_ERROR ) ==  SX126X_IRQ_CRC_ERROR )
         {
            sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC );
            RxError();
         }

        if( ( radioFlag &  SX126X_IRQ_CAD_DONE ) ==  SX126X_IRQ_CAD_DONE )
         {
             sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC );
             CadDone((radioFlag & SX126X_IRQ_CAD_DETECTED) == SX126X_IRQ_CAD_DETECTED);
         }

         if((radioFlag & SX126X_IRQ_TIMEOUT) == SX126X_IRQ_TIMEOUT)
         {
             sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC );
            if( sx1262GetOperatingMode( ) == MODE_TX )
            {
                TxTimeout();
                
            }else if( sx1262GetOperatingMode( ) == MODE_RX )
            {
                RxTimeout();
            }            
        }

        if( ( radioFlag & SX126X_IRQ_PREAMBLE_DETECTED ) == SX126X_IRQ_PREAMBLE_DETECTED )
        {
             //__NOP( );
        }
        
        if( ( radioFlag & SX126X_IRQ_SYNC_WORD_VALID ) == SX126X_IRQ_SYNC_WORD_VALID )
        {
             //__NOP( );
        }
        
        if( ( radioFlag & SX126X_IRQ_HEADER_VALID ) == SX126X_IRQ_HEADER_VALID )
        {
              //__NOP( );
        }
               
        if( ( radioFlag & SX126X_IRQ_HEADER_ERROR ) == SX126X_IRQ_HEADER_ERROR )
        {        
            RxTimeout();
        }


    }

}
