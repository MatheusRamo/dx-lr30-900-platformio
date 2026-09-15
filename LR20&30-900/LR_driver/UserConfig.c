#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "main.h"
#include "driver_timer.h"
#include "UserConfig.h"
#include "sx126x.h"

static volatile uint8_t radio_receiving;
static volatile uint8_t radio_tx_done;
static volatile RadioOperatingModes_t operating_mode;
volatile uint8_t IrqFired = false;
sx126x_rx_buffer_status_t offset = {0};
sx126x_pkt_status_lora_t RadioPktStatus;
sx126x_irq_mask_t radioFlag = 0;
static uint8_t radio_rx_buffer[255];

LoRaConfig_t g_lora_config = {
    .frequency_hz=LORA_DEFAULT_FREQUENCY_HZ,
    .bandwidth_hz=LORA_DEFAULT_BANDWIDTH_HZ,
    .spreading_factor=LORA_DEFAULT_SPREADING_FACTOR,
    .coding_rate=LORA_DEFAULT_CODING_RATE,
    .tx_power_dbm=LORA_DEFAULT_TX_POWER_DBM,
    .preamble_length=LORA_DEFAULT_PREAMBLE_LENGTH,
    .crc_enabled=LORA_DEFAULT_CRC_ENABLED
};

static bool ConvertBandwidth(uint32_t hz, sx126x_lora_bw_t *value) {
    switch(hz) { case 125000UL:*value=SX126X_LORA_BW_125;return true;case 250000UL:*value=SX126X_LORA_BW_250;return true;case 500000UL:*value=SX126X_LORA_BW_500;return true;default:return false; }
}
static bool ConvertSpreadingFactor(uint8_t sf,sx126x_lora_sf_t*value){if(sf<5U||sf>12U)return false;*value=(sx126x_lora_sf_t)sf;return true;}
static bool ConvertCodingRate(uint8_t cr,sx126x_lora_cr_t*value){switch(cr){case 5:*value=SX126X_LORA_CR_4_5;return true;case 6:*value=SX126X_LORA_CR_4_6;return true;case 7:*value=SX126X_LORA_CR_4_7;return true;case 8:*value=SX126X_LORA_CR_4_8;return true;default:return false;}}
static bool CalculateLdro(uint8_t sf,uint32_t bandwidth){return ((((uint64_t)1U<<sf)*1000000ULL)/bandwidth)>=16384ULL;}

static bool ApplyRxPacketParams(void) {
    sx126x_pkt_params_lora_t p={.preamble_len_in_symb=g_lora_config.preamble_length,.header_type=SX126X_LORA_PKT_EXPLICIT,.pld_len_in_bytes=255U,.crc_is_on=g_lora_config.crc_enabled,.invert_iq_is_on=LORA_IQ_INVERSION_ON};
    return sx126x_set_lora_pkt_params(NULL,&p)==SX126X_STATUS_OK;
}
static bool ApplyTxPacketParams(uint8_t length) {
    sx126x_pkt_params_lora_t p={.preamble_len_in_symb=g_lora_config.preamble_length,.header_type=SX126X_LORA_PKT_EXPLICIT,.pld_len_in_bytes=length,.crc_is_on=g_lora_config.crc_enabled,.invert_iq_is_on=LORA_IQ_INVERSION_ON};
    return sx126x_set_lora_pkt_params(NULL,&p)==SX126X_STATUS_OK;
}

void gpio_init(void) {
    GPIO_InitTypeDef gpio={0};__HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin=LCC68_BUSY_PIN;gpio.Mode=GPIO_MODE_INPUT;gpio.Pull=GPIO_PULLUP;gpio.Speed=GPIO_SPEED_FREQ_HIGH;HAL_GPIO_Init(LCC68_BUSY_PORT,&gpio);
    gpio.Pin=LCC68_NRST_PIN|LCC68_RXEN_PIN|LCC68_TXEN_PIN;gpio.Mode=GPIO_MODE_OUTPUT_PP;gpio.Pull=GPIO_PULLUP;HAL_GPIO_Init(GPIOA,&gpio);
    HAL_GPIO_WritePin(LCC68_NRST_PORT,LCC68_NRST_PIN,GPIO_PIN_SET);HAL_GPIO_WritePin(LCC68_RXEN_PORT,LCC68_RXEN_PIN,GPIO_PIN_SET);HAL_GPIO_WritePin(LCC68_TXEN_PORT,LCC68_TXEN_PIN,GPIO_PIN_RESET);ms_timer_delay(20);
}
RadioOperatingModes_t sx1262GetOperatingMode(void){return operating_mode;}
void sx1262SetOperatingMode(RadioOperatingModes_t mode){operating_mode=mode;}
static void RxEn(void){HAL_GPIO_WritePin(LCC68_RXEN_PORT,LCC68_RXEN_PIN,GPIO_PIN_SET);HAL_GPIO_WritePin(LCC68_TXEN_PORT,LCC68_TXEN_PIN,GPIO_PIN_RESET);}
static void TxEn(void){HAL_GPIO_WritePin(LCC68_RXEN_PORT,LCC68_RXEN_PIN,GPIO_PIN_RESET);HAL_GPIO_WritePin(LCC68_TXEN_PORT,LCC68_TXEN_PIN,GPIO_PIN_SET);}

static void OpenRx(void) {
    radio_receiving=true;RxEn();(void)ApplyRxPacketParams();
    (void)sx126x_set_rx_with_timeout_in_rtc_step(NULL,SX126X_RX_CONTINUOUS);sx1262SetOperatingMode(MODE_RX);
}
bool LoRaApplyConfig(void) {
    sx126x_lora_bw_t bw;sx126x_lora_sf_t sf;sx126x_lora_cr_t cr;sx126x_mod_params_lora_t p;
    if(g_lora_config.frequency_hz<850000000UL||g_lora_config.frequency_hz>930000000UL||g_lora_config.tx_power_dbm<(-9)||g_lora_config.tx_power_dbm>22||g_lora_config.preamble_length<4U||!ConvertBandwidth(g_lora_config.bandwidth_hz,&bw)||!ConvertSpreadingFactor(g_lora_config.spreading_factor,&sf)||!ConvertCodingRate(g_lora_config.coding_rate,&cr))return false;
    p.bw=bw;p.sf=sf;p.cr=cr;p.ldro=CalculateLdro(g_lora_config.spreading_factor,g_lora_config.bandwidth_hz)?1U:0U;radio_receiving=false;
    if(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC)!=SX126X_STATUS_OK||sx126x_set_rf_freq(NULL,g_lora_config.frequency_hz)!=SX126X_STATUS_OK||sx126x_set_lora_mod_params(NULL,&p)!=SX126X_STATUS_OK||sx126x_set_tx_params(NULL,g_lora_config.tx_power_dbm,SX126X_RAMP_3400_US)!=SX126X_STATUS_OK||!ApplyRxPacketParams())return false;
    OpenRx();return true;
}
bool LoraSetConfig(const LoRaConfig_t*config){LoRaConfig_t old;if(!config)return false;old=g_lora_config;g_lora_config=*config;if(LoRaApplyConfig())return true;g_lora_config=old;(void)LoRaApplyConfig();return false;}

void LoraInit(void) {
    sx126x_pa_cfg_params_t pa={.pa_duty_cycle=0x04,.hp_max=0x07,.device_sel=0x00,.pa_lut=0x01};radio_tx_done=true;radio_receiving=false;
    sx126x_reset(NULL);sx126x_wakeup(NULL);sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC);sx126x_set_standby(NULL,SX126X_STANDBY_CFG_XOSC);sx126x_set_reg_mode(NULL,SX126X_REG_MODE_DCDC);sx126x_set_buffer_base_address(NULL,0,0);sx126x_set_pkt_type(NULL,SX126X_PKT_TYPE_LORA);sx126x_set_trimming_capacitor_values(NULL,0x4,0x2f);sx126x_set_pa_cfg(NULL,&pa);
    sx126x_set_dio_irq_params(NULL,SX126X_IRQ_RX_DONE|SX126X_IRQ_TX_DONE|SX126X_IRQ_TIMEOUT|SX126X_IRQ_CRC_ERROR|SX126X_IRQ_HEADER_ERROR,SX126X_IRQ_RX_DONE|SX126X_IRQ_TX_DONE|SX126X_IRQ_TIMEOUT|SX126X_IRQ_CRC_ERROR|SX126X_IRQ_HEADER_ERROR,SX126X_IRQ_NONE,SX126X_IRQ_NONE);sx126x_clear_irq_status(NULL,SX126X_IRQ_ALL);(void)LoRaApplyConfig();
}
bool LoraDataSend(const uint8_t*data,uint8_t length) {
    sx126x_mod_params_lora_t mod;
    sx126x_pkt_params_lora_t pkt;
    uint32_t timeout_ms;
    if(!data||!length||!radio_tx_done) return false;
    if(!ConvertBandwidth(g_lora_config.bandwidth_hz,&mod.bw)||!ConvertSpreadingFactor(g_lora_config.spreading_factor,&mod.sf)||!ConvertCodingRate(g_lora_config.coding_rate,&mod.cr))return false;
    mod.ldro=CalculateLdro(g_lora_config.spreading_factor,g_lora_config.bandwidth_hz)?1U:0U;
    memset(&pkt,0,sizeof(pkt));pkt.preamble_len_in_symb=g_lora_config.preamble_length;
    pkt.header_type=SX126X_LORA_PKT_EXPLICIT;pkt.pld_len_in_bytes=length;pkt.crc_is_on=g_lora_config.crc_enabled;
    timeout_ms=sx126x_get_lora_time_on_air_in_ms(&pkt,&mod)+150U;
    TxEn();
    if(!ApplyTxPacketParams(length)||sx126x_write_buffer(NULL,0,data,length)!=SX126X_STATUS_OK||sx126x_set_tx(NULL,timeout_ms)!=SX126X_STATUS_OK){OpenRx();return false;}
    radio_tx_done=false;radio_receiving=false;sx1262SetOperatingMode(MODE_TX);return true;
}
bool LoraTxBusy(void){return !radio_tx_done;}

static void OnTxDone(void){radio_tx_done=true;OpenRx();Modem_OnRadioTxDone(true);}
static void OnRxDone(uint8_t*payload,uint8_t length,int16_t rssi,int8_t snr){radio_tx_done=true;Modem_OnRadioRx(payload,length,rssi,snr);OpenRx();}
static void OnTxTimeout(void){radio_tx_done=true;OpenRx();Modem_OnRadioTxDone(false);}

void DX_Lora_RadioIrqProcess(void) {
    sx126x_irq_mask_t flags;
    if(!IrqFired)return;
    __disable_irq();IrqFired=false;__enable_irq();
    if(sx126x_get_irq_status(NULL,&flags)!=SX126X_STATUS_OK)return;
    (void)sx126x_clear_irq_status(NULL,flags);radioFlag=flags;
    if((flags&SX126X_IRQ_TX_DONE)!=0U){(void)sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC);OnTxDone();}
    if((flags&(SX126X_IRQ_CRC_ERROR|SX126X_IRQ_HEADER_ERROR))!=0U){(void)sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC);radio_tx_done=true;OpenRx();return;}
    if((flags&SX126X_IRQ_RX_DONE)!=0U){
        (void)sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC);
        if(sx126x_get_rx_buffer_status(NULL,&offset)==SX126X_STATUS_OK&&offset.pld_len_in_bytes<=sizeof(radio_rx_buffer)&&sx126x_read_buffer(NULL,offset.buffer_start_pointer,radio_rx_buffer,offset.pld_len_in_bytes)==SX126X_STATUS_OK&&sx126x_get_lora_pkt_status(NULL,&RadioPktStatus)==SX126X_STATUS_OK)
            OnRxDone(radio_rx_buffer,offset.pld_len_in_bytes,RadioPktStatus.rssi_pkt_in_dbm,RadioPktStatus.snr_pkt_in_db);
        else OpenRx();
    }
    if((flags&SX126X_IRQ_TIMEOUT)!=0U){(void)sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC);if(sx1262GetOperatingMode()==MODE_TX)OnTxTimeout();else OpenRx();}
}
