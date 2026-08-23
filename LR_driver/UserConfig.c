



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

LoRaConfig_t g_lora_config =
{
    .frequency_hz = LORA_DEFAULT_FREQUENCY_HZ,
    .bandwidth_hz = LORA_DEFAULT_BANDWIDTH_HZ,
    .spreading_factor = LORA_DEFAULT_SPREADING_FACTOR,
    .coding_rate = LORA_DEFAULT_CODING_RATE,
    .tx_power_dbm = LORA_DEFAULT_TX_POWER_DBM,
    .preamble_length = LORA_DEFAULT_PREAMBLE_LENGTH,
    .crc_enabled = LORA_DEFAULT_CRC_ENABLED
};


uint8_t IrqFired = false;
sx126x_rx_buffer_status_t offset = {0};
sx126x_pkt_status_lora_t RadioPktStatus;
sx126x_irq_mask_t radioFlag = 0;

static volatile RadioOperatingModes_t OperatingMode;
void LoraOpenRXMode(uint8_t Timerout);

static bool ConvertBandwidth(uint32_t bandwidth_hz, sx126x_lora_bw_t *result)
{
    switch(bandwidth_hz)
    {
        case 125000UL: *result = SX126X_LORA_BW_125; return true;
        case 250000UL: *result = SX126X_LORA_BW_250; return true;
        case 500000UL: *result = SX126X_LORA_BW_500; return true;
        default: return false;
    }
}

static bool ConvertSpreadingFactor(uint8_t sf, sx126x_lora_sf_t *result)
{
    if((sf < 5U) || (sf > 12U))
    {
        return false;
    }

    *result = (sx126x_lora_sf_t)sf;
    return true;
}

static bool ConvertCodingRate(uint8_t cr, sx126x_lora_cr_t *result)
{
    switch(cr)
    {
        case 5U: *result = SX126X_LORA_CR_4_5; return true;
        case 6U: *result = SX126X_LORA_CR_4_6; return true;
        case 7U: *result = SX126X_LORA_CR_4_7; return true;
        case 8U: *result = SX126X_LORA_CR_4_8; return true;
        default: return false;
    }
}

static bool LoRaCalculateLdro(uint8_t sf, uint32_t bandwidth_hz)
{
    const uint64_t symbol_duration_us =
        (((uint64_t)1U << sf) * 1000000ULL) / bandwidth_hz;

    return symbol_duration_us >= 16384ULL;
}

static bool LoRaApplyRxPacketParams(void)
{
    sx126x_pkt_params_lora_t params =
    {
        .preamble_len_in_symb = g_lora_config.preamble_length,
        .header_type = SX126X_LORA_PKT_EXPLICIT,
        .pld_len_in_bytes = 0xFFU,
        .crc_is_on = g_lora_config.crc_enabled,
        .invert_iq_is_on = LORA_IQ_INVERSION_ON
    };

    return sx126x_set_lora_pkt_params(NULL, &params) == SX126X_STATUS_OK;
}

static bool LoRaApplyTxPacketParams(uint8_t payload_length)
{
    sx126x_pkt_params_lora_t params =
    {
        .preamble_len_in_symb = g_lora_config.preamble_length,
        .header_type = SX126X_LORA_PKT_EXPLICIT,
        .pld_len_in_bytes = payload_length,
        .crc_is_on = g_lora_config.crc_enabled,
        .invert_iq_is_on = LORA_IQ_INVERSION_ON
    };

    return sx126x_set_lora_pkt_params(NULL, &params) == SX126X_STATUS_OK;
}



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

bool LoRaApplyConfig(void)
{
    sx126x_lora_bw_t bandwidth;
    sx126x_lora_sf_t spreading_factor;
    sx126x_lora_cr_t coding_rate;
    sx126x_mod_params_lora_t modulation_params;

    if((g_lora_config.frequency_hz < 850000000UL) ||
       (g_lora_config.frequency_hz > 930000000UL) ||
       (g_lora_config.tx_power_dbm < -9) ||
       (g_lora_config.tx_power_dbm > 22) ||
       (g_lora_config.preamble_length < 4U) ||
       !ConvertBandwidth(g_lora_config.bandwidth_hz, &bandwidth) ||
       !ConvertSpreadingFactor(g_lora_config.spreading_factor, &spreading_factor) ||
       !ConvertCodingRate(g_lora_config.coding_rate, &coding_rate))
    {
        return false;
    }

    modulation_params.bw = bandwidth;
    modulation_params.sf = spreading_factor;
    modulation_params.cr = coding_rate;
    modulation_params.ldro = LoRaCalculateLdro(
        g_lora_config.spreading_factor,
        g_lora_config.bandwidth_hz
    ) ? 1U : 0U;

    g_lora_test_rxs = false;

    if(sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC) != SX126X_STATUS_OK ||
       sx126x_set_rf_freq(NULL, g_lora_config.frequency_hz) != SX126X_STATUS_OK ||
       sx126x_set_lora_mod_params(NULL, &modulation_params) != SX126X_STATUS_OK ||
       sx126x_set_tx_params(NULL, g_lora_config.tx_power_dbm, SX126X_RAMP_3400_US) != SX126X_STATUS_OK ||
       !LoRaApplyRxPacketParams() ||
       sx126x_set_rx(NULL, LORA_SX126x_SYMBOL_TIMEOUT) != SX126X_STATUS_OK)
    {
        return false;
    }

    sx1262SetOperatingMode(MODE_RX);
    RxEn();
    g_lora_test_rxs = true;
    return true;
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

    sx126x_pa_cfg_params_t  params3;
    params3.pa_duty_cycle = 0x04;
    params3.hp_max = 0x07;
    params3.device_sel = 0x00;
    params3.pa_lut = 0x01;
    sx126x_set_pa_cfg(NULL, &params3);
    		
    //打开dio1的中断 中断触发 SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE
    sx126x_set_dio_irq_params(NULL, SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE,SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE, SX126X_IRQ_NONE, SX126X_IRQ_NONE);
    sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL);
	
	  /* 设置载波频率(频点) */
	 #if TEST
        (void)LoRaApplyConfig();
        g_lora_test_rxs = true;
        TxEn();
    #else
        if(!LoRaApplyConfig())
        {
            printf("ERR|CMD=INIT|CODE=RADIO_ERROR\r\n");
        }
    #endif
}


void LoraDataSend(uint8_t *data,uint8_t len)
{
   TxEn();
   if(!LoRaApplyTxPacketParams(len))
   {
       LoraOpenRXMode(LORA_SX126x_SYMBOL_TIMEOUT);
       return;
   }
   sx126x_write_buffer(NULL, 0x00, data, len);
   sx126x_set_tx(NULL,6000);
   g_lora_tx_done = false;
   g_lora_test_rxs = false;
   sx1262SetOperatingMode(MODE_TX);
}




void LoraOpenRXMode(uint8_t Timerout)
{
     g_lora_test_rxs = true;
     (void)LoRaApplyRxPacketParams();
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

static bool IsATCommand(const uint8_t *data, uint8_t length)
{
    return (length >= 3U) &&
           (data[0] == (uint8_t)'A') &&
           (data[1] == (uint8_t)'T') &&
           (data[2] == (uint8_t)'+');
}

static bool ParseUnsignedValue(const char *text, unsigned long *value)
{
    char *end;

    if((text == NULL) || (*text == '\0') || (*text == '+') || (*text == '-'))
    {
        return false;
    }

    *value = strtoul(text, &end, 10);
    return (*end == '\0');
}

static bool ParseSignedValue(const char *text, long *value)
{
    char *end;

    if((text == NULL) || (*text == '\0') || (*text == '+'))
    {
        return false;
    }

    *value = strtol(text, &end, 10);
    return (*end == '\0');
}

static bool ApplyCandidateConfig(const char *command_name, const LoRaConfig_t *candidate)
{
    LoRaConfig_t previous = g_lora_config;

    g_lora_config = *candidate;
    if(LoRaApplyConfig())
    {
        return true;
    }

    g_lora_config = previous;
    if(!LoRaApplyConfig())
    {
        printf("ERR|CMD=%s|CODE=ROLLBACK_FAILED\r\n", command_name);
    }
    else
    {
        printf("ERR|CMD=%s|CODE=RADIO_ERROR\r\n", command_name);
    }

    return false;
}

static void PrintInvalidValue(const char *command_name)
{
    printf("ERR|CMD=%s|CODE=INVALID_VALUE\r\n", command_name);
}

static void ProcessATCommand(char *command)
{
    LoRaConfig_t candidate = g_lora_config;
    unsigned long unsigned_value;
    long signed_value;

    if(strcmp(command, "AT+STATUS") == 0)
    {
        printf(
            "STATUS|FREQ=%lu|BW=%lu|SF=%u|CR=%u|POWER=%d|PREAMBLE=%u|CRC=%u\r\n",
            (unsigned long)g_lora_config.frequency_hz,
            (unsigned long)g_lora_config.bandwidth_hz,
            (unsigned int)g_lora_config.spreading_factor,
            (unsigned int)g_lora_config.coding_rate,
            g_lora_config.tx_power_dbm,
            (unsigned int)g_lora_config.preamble_length,
            g_lora_config.crc_enabled ? 1U : 0U
        );
        return;
    }

    if(strncmp(command, "AT+FREQ=", 8U) == 0)
    {
        if(!ParseUnsignedValue(command + 8, &unsigned_value) ||
           (unsigned_value < 850000000UL) || (unsigned_value > 930000000UL))
        {
            PrintInvalidValue("FREQ");
            return;
        }
        candidate.frequency_hz = (uint32_t)unsigned_value;
        if(ApplyCandidateConfig("FREQ", &candidate))
        {
            printf("OK|FREQ=%lu\r\n", unsigned_value);
        }
        return;
    }

    if(strncmp(command, "AT+BW=", 6U) == 0)
    {
        if(!ParseUnsignedValue(command + 6, &unsigned_value) ||
           ((unsigned_value != 125000UL) &&
            (unsigned_value != 250000UL) &&
            (unsigned_value != 500000UL)))
        {
            PrintInvalidValue("BW");
            return;
        }
        candidate.bandwidth_hz = (uint32_t)unsigned_value;
        if(ApplyCandidateConfig("BW", &candidate))
        {
            printf("OK|BW=%lu\r\n", unsigned_value);
        }
        return;
    }

    if(strncmp(command, "AT+SF=", 6U) == 0)
    {
        if(!ParseUnsignedValue(command + 6, &unsigned_value) ||
           (unsigned_value < 5UL) || (unsigned_value > 12UL))
        {
            PrintInvalidValue("SF");
            return;
        }
        candidate.spreading_factor = (uint8_t)unsigned_value;
        if(ApplyCandidateConfig("SF", &candidate))
        {
            printf("OK|SF=%lu\r\n", unsigned_value);
        }
        return;
    }

    if(strncmp(command, "AT+CR=", 6U) == 0)
    {
        if(!ParseUnsignedValue(command + 6, &unsigned_value) ||
           (unsigned_value < 5UL) || (unsigned_value > 8UL))
        {
            PrintInvalidValue("CR");
            return;
        }
        candidate.coding_rate = (uint8_t)unsigned_value;
        if(ApplyCandidateConfig("CR", &candidate))
        {
            printf("OK|CR=%lu\r\n", unsigned_value);
        }
        return;
    }

    if(strncmp(command, "AT+POWER=", 9U) == 0)
    {
        if(!ParseSignedValue(command + 9, &signed_value) ||
           (signed_value < -9L) || (signed_value > 22L))
        {
            PrintInvalidValue("POWER");
            return;
        }
        candidate.tx_power_dbm = (int8_t)signed_value;
        if(ApplyCandidateConfig("POWER", &candidate))
        {
            printf("OK|POWER=%ld\r\n", signed_value);
        }
        return;
    }

    if(strncmp(command, "AT+PREAMBLE=", 12U) == 0)
    {
        if(!ParseUnsignedValue(command + 12, &unsigned_value) ||
           (unsigned_value < 4UL) || (unsigned_value > 65535UL))
        {
            PrintInvalidValue("PREAMBLE");
            return;
        }
        candidate.preamble_length = (uint16_t)unsigned_value;
        if(ApplyCandidateConfig("PREAMBLE", &candidate))
        {
            printf("OK|PREAMBLE=%lu\r\n", unsigned_value);
        }
        return;
    }

    if(strncmp(command, "AT+CRC=", 7U) == 0)
    {
        if(!ParseUnsignedValue(command + 7, &unsigned_value) ||
           (unsigned_value > 1UL))
        {
            PrintInvalidValue("CRC");
            return;
        }
        candidate.crc_enabled = (unsigned_value == 1UL);
        if(ApplyCandidateConfig("CRC", &candidate))
        {
            printf("OK|CRC=%lu\r\n", unsigned_value);
        }
        return;
    }

    if(strcmp(command, "AT+DEFAULTS") == 0)
    {
        candidate.frequency_hz = LORA_DEFAULT_FREQUENCY_HZ;
        candidate.bandwidth_hz = LORA_DEFAULT_BANDWIDTH_HZ;
        candidate.spreading_factor = LORA_DEFAULT_SPREADING_FACTOR;
        candidate.coding_rate = LORA_DEFAULT_CODING_RATE;
        candidate.tx_power_dbm = LORA_DEFAULT_TX_POWER_DBM;
        candidate.preamble_length = LORA_DEFAULT_PREAMBLE_LENGTH;
        candidate.crc_enabled = LORA_DEFAULT_CRC_ENABLED;
        if(ApplyCandidateConfig("DEFAULTS", &candidate))
        {
            printf("OK|DEFAULTS=1\r\n");
        }
        return;
    }

    printf("ERR|CMD=UNKNOWN\r\n");
}

void Data_Processing(void)
{

	if(!queueIsEmpty(pUart1RxQueue)&& g_lora_tx_done == true && g_lora_test_rxs == true)
    {
        #if !TEST
       uint8_t mydata[SIZE_DATA] = {0};
       uint8_t len = queueDequeue(pUart1RxQueue, &mydata);
       //发送数据
       if(IsATCommand(mydata, len))
       {
           char command[SIZE_DATA + 1U];
           uint16_t command_length = len;

           memcpy(command, mydata, len);
           while((command_length > 0U) &&
                 ((command[command_length - 1U] == '\r') ||
                  (command[command_length - 1U] == '\n') ||
                  (command[command_length - 1U] == ' ') ||
                  (command[command_length - 1U] == '\t')))
           {
               command_length--;
           }
           command[command_length] = '\0';
           ProcessATCommand(command);
       }
       else
       {
           LoraDataSend(mydata, len);
       }
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
