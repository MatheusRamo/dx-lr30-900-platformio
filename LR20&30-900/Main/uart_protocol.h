#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define UART_PROTO_SYNC0 0xA5U
#define UART_PROTO_SYNC1 0x5AU
#define UART_PROTO_VERSION 0x01U
#define UART_PROTO_MAX_PAYLOAD 260U

typedef enum {
    UART_MSG_HELLO_REQ = 0x01, UART_MSG_HELLO_RESP = 0x81,
    UART_MSG_RADIO_CONFIG_SET = 0x02, UART_MSG_RADIO_CONFIG_RESULT = 0x82,
    UART_MSG_RADIO_STATUS_REQ = 0x03, UART_MSG_RADIO_STATUS_RESP = 0x83,
    UART_MSG_RADIO_TX_PACKET = 0x10, UART_MSG_RADIO_TX_RESULT = 0x90,
    UART_MSG_RADIO_RX_PACKET = 0x91, UART_MSG_ERROR = 0x7F
} UartMessageType_t;

typedef enum {
    UART_RESULT_OK = 0, UART_RESULT_BUSY = 1, UART_RESULT_INVALID = 2,
    UART_RESULT_RADIO_ERROR = 3, UART_RESULT_UNSUPPORTED = 4
} UartResult_t;

void UartProtocol_Init(void);
void UartProtocol_RxByteFromIsr(uint8_t byte);
void UartProtocol_TxCompleteFromIsr(void);
void UartProtocol_Process(void);
bool UartProtocol_Send(uint8_t type, uint16_t sequence,
                       const uint8_t *payload, uint16_t length);
uint16_t UartProtocol_Crc16(const uint8_t *data, uint16_t length);
void UartProtocol_OnFrame(uint8_t type, uint16_t sequence,
                          const uint8_t *payload, uint16_t length);

#endif
