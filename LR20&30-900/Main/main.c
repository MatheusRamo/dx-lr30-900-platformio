#include <string.h>
#include "main.h"
#include "driver_usart.h"
#include "driver_spi.h"
#include "driver_key.h"
#include "driver_timer.h"
#include "UserConfig.h"
#include "uart_protocol.h"

uint8_t pdata = 0U;
static uint16_t active_tx_sequence;

static uint32_t ReadLe32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void WriteLe16(uint8_t *p, uint16_t value) { p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); }
static void WriteLe32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static void SendResult(uint8_t type, uint16_t sequence, UartResult_t result) {
    uint8_t value = (uint8_t)result; (void)UartProtocol_Send(type, sequence, &value, 1U);
}

void UartProtocol_OnFrame(uint8_t type, uint16_t sequence, const uint8_t *payload, uint16_t length) {
    uint8_t response[20]; LoRaConfig_t candidate;
    switch(type) {
        case UART_MSG_HELLO_REQ:
            response[0] = 'L'; response[1] = 'R'; response[2] = '3'; response[3] = '0';
            response[4] = 1U; response[5] = 255U;
            (void)UartProtocol_Send(UART_MSG_HELLO_RESP, sequence, response, 6U);
            break;
        case UART_MSG_RADIO_CONFIG_SET:
            if(length != 14U || LoraTxBusy()) {
                SendResult(UART_MSG_RADIO_CONFIG_RESULT, sequence, LoraTxBusy() ? UART_RESULT_BUSY : UART_RESULT_INVALID); break;
            }
            candidate.frequency_hz = ReadLe32(&payload[0]); candidate.bandwidth_hz = ReadLe32(&payload[4]);
            candidate.spreading_factor = payload[8]; candidate.coding_rate = payload[9]; candidate.tx_power_dbm = (int8_t)payload[10];
            candidate.preamble_length = (uint16_t)payload[11] | ((uint16_t)payload[12] << 8); candidate.crc_enabled = payload[13] != 0U;
            SendResult(UART_MSG_RADIO_CONFIG_RESULT, sequence, LoraSetConfig(&candidate) ? UART_RESULT_OK : UART_RESULT_RADIO_ERROR);
            break;
        case UART_MSG_RADIO_STATUS_REQ:
            response[0] = LoraTxBusy() ? 1U : 0U; WriteLe32(&response[1], g_lora_config.frequency_hz); WriteLe32(&response[5], g_lora_config.bandwidth_hz);
            response[9] = g_lora_config.spreading_factor; response[10] = g_lora_config.coding_rate; response[11] = (uint8_t)g_lora_config.tx_power_dbm;
            WriteLe16(&response[12], g_lora_config.preamble_length); response[14] = g_lora_config.crc_enabled ? 1U : 0U;
            (void)UartProtocol_Send(UART_MSG_RADIO_STATUS_RESP, sequence, response, 15U);
            break;
        case UART_MSG_RADIO_TX_PACKET:
            if(length == 0U || length > 255U) SendResult(UART_MSG_RADIO_TX_RESULT, sequence, UART_RESULT_INVALID);
            else if(LoraTxBusy()) SendResult(UART_MSG_RADIO_TX_RESULT, sequence, UART_RESULT_BUSY);
            else { active_tx_sequence = sequence; if(!LoraDataSend(payload, (uint8_t)length)) SendResult(UART_MSG_RADIO_TX_RESULT, sequence, UART_RESULT_RADIO_ERROR); }
            break;
        default:
            response[0] = UART_RESULT_UNSUPPORTED; response[1] = type;
            (void)UartProtocol_Send(UART_MSG_ERROR, sequence, response, 2U); break;
    }
}

void Modem_OnRadioTxDone(bool success) {
    SendResult(UART_MSG_RADIO_TX_RESULT, active_tx_sequence, success ? UART_RESULT_OK : UART_RESULT_RADIO_ERROR);
}
void Modem_OnRadioRx(const uint8_t *payload, uint8_t length, int16_t rssi, int8_t snr) {
    static uint16_t rx_sequence; uint8_t response[259];
    response[0] = (uint8_t)rssi; response[1] = (uint8_t)((uint16_t)rssi >> 8); response[2] = (uint8_t)snr; response[3] = length;
    memcpy(&response[4], payload, length);
    (void)UartProtocol_Send(UART_MSG_RADIO_RX_PACKET, rx_sequence++, response, (uint16_t)length + 4U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if(huart == &husart) { UartProtocol_RxByteFromIsr(pdata); (void)HAL_UART_Receive_IT(&husart, &pdata, 1U); }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if(huart == &husart) UartProtocol_TxCompleteFromIsr();
}

int main(void) {
    HAL_Init(); SystemClock_Config(); UartProtocol_Init(); UsartInit(115200U);
    TimerInit(); SPI_Init(); gpio_init(); LoraInit(); DIO1_Init(); IrqFired = false; radioFlag = 0U;
    for(;;) { UartProtocol_Process(); DX_Lora_RadioIrqProcess(); }
}

void Error_Handler(void) { __disable_irq(); for(;;) { } }
