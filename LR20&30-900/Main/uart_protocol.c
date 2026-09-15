#include "uart_protocol.h"
#include <string.h>
#include "driver_usart.h"

#define RX_RING_SIZE 512U
#define TX_RING_SIZE 1024U
typedef enum { P_SYNC0, P_SYNC1, P_HEADER, P_PAYLOAD, P_CRC0, P_CRC1 } ParserState_t;

static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head, rx_tail;
static uint8_t tx_ring[TX_RING_SIZE];
static volatile uint16_t tx_head, tx_tail, tx_active_length;
static ParserState_t state;
static uint8_t header[6], header_position;
static uint8_t payload[UART_PROTO_MAX_PAYLOAD];
static uint16_t payload_position, payload_length, received_crc;

static uint16_t RingUsed(uint16_t head, uint16_t tail, uint16_t size) {
    return (head >= tail) ? (head - tail) : (uint16_t)(size - tail + head);
}

static uint16_t CrcUpdate(uint16_t crc, const uint8_t *data, uint16_t length) {
    uint16_t i; uint8_t bit;
    for(i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0; bit < 8U; ++bit)
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t UartProtocol_Crc16(const uint8_t *data, uint16_t length) {
    return CrcUpdate(0xFFFFU, data, length);
}

static void ParserResetWithCandidate(uint8_t byte) {
    state = (byte == UART_PROTO_SYNC0) ? P_SYNC1 : P_SYNC0;
    header_position = 0U; payload_position = 0U; payload_length = 0U;
}

static void TxKick(void) {
    uint16_t length;
    __disable_irq();
    if(tx_active_length != 0U || tx_head == tx_tail) { __enable_irq(); return; }
    length = (tx_head > tx_tail) ? (uint16_t)(tx_head - tx_tail) : (uint16_t)(TX_RING_SIZE - tx_tail);
    tx_active_length = length;
    if(HAL_UART_Transmit_IT(&husart, &tx_ring[tx_tail], length) != HAL_OK) tx_active_length = 0U;
    __enable_irq();
}

void UartProtocol_Init(void) {
    rx_head = rx_tail = tx_head = tx_tail = tx_active_length = 0U;
    ParserResetWithCandidate(0U);
}

void UartProtocol_RxByteFromIsr(uint8_t byte) {
    uint16_t next = (uint16_t)((rx_head + 1U) % RX_RING_SIZE);
    if(next != rx_tail) { rx_ring[rx_head] = byte; rx_head = next; }
}

void UartProtocol_TxCompleteFromIsr(void) {
    tx_tail = (uint16_t)((tx_tail + tx_active_length) % TX_RING_SIZE);
    tx_active_length = 0U; TxKick();
}

bool UartProtocol_Send(uint8_t type, uint16_t sequence, const uint8_t *data, uint16_t length) {
    uint8_t frame[UART_PROTO_MAX_PAYLOAD + 10U];
    uint16_t frame_length, crc, i, free_bytes;
    if(length > UART_PROTO_MAX_PAYLOAD || (length > 0U && data == NULL)) return false;
    frame[0] = UART_PROTO_SYNC0; frame[1] = UART_PROTO_SYNC1;
    frame[2] = UART_PROTO_VERSION; frame[3] = type;
    frame[4] = (uint8_t)sequence; frame[5] = (uint8_t)(sequence >> 8);
    frame[6] = (uint8_t)length; frame[7] = (uint8_t)(length >> 8);
    if(length) memcpy(&frame[8], data, length);
    crc = UartProtocol_Crc16(&frame[2], (uint16_t)(6U + length));
    frame[8U + length] = (uint8_t)crc; frame[9U + length] = (uint8_t)(crc >> 8);
    frame_length = (uint16_t)(10U + length);
    __disable_irq();
    free_bytes = (uint16_t)(TX_RING_SIZE - 1U - RingUsed(tx_head, tx_tail, TX_RING_SIZE));
    if(free_bytes < frame_length) { __enable_irq(); return false; }
    for(i = 0; i < frame_length; ++i) { tx_ring[tx_head] = frame[i]; tx_head = (uint16_t)((tx_head + 1U) % TX_RING_SIZE); }
    __enable_irq(); TxKick(); return true;
}

static void ParserConsume(uint8_t byte) {
    uint16_t expected, sequence;
    switch(state) {
        case P_SYNC0: if(byte == UART_PROTO_SYNC0) state = P_SYNC1; break;
        case P_SYNC1:
            if(byte == UART_PROTO_SYNC1) { state = P_HEADER; header_position = 0U; }
            else ParserResetWithCandidate(byte);
            break;
        case P_HEADER:
            header[header_position++] = byte;
            if(header_position == sizeof(header)) {
                payload_length = (uint16_t)header[4] | ((uint16_t)header[5] << 8);
                payload_position = 0U;
                if(header[0] != UART_PROTO_VERSION || payload_length > UART_PROTO_MAX_PAYLOAD) ParserResetWithCandidate(byte);
                else state = payload_length ? P_PAYLOAD : P_CRC0;
            }
            break;
        case P_PAYLOAD: payload[payload_position++] = byte; if(payload_position == payload_length) state = P_CRC0; break;
        case P_CRC0: received_crc = byte; state = P_CRC1; break;
        case P_CRC1:
            received_crc |= (uint16_t)byte << 8;
            expected = CrcUpdate(CrcUpdate(0xFFFFU, header, sizeof(header)), payload, payload_length);
            if(expected == received_crc) {
                sequence = (uint16_t)header[2] | ((uint16_t)header[3] << 8);
                UartProtocol_OnFrame(header[1], sequence, payload, payload_length);
            }
            ParserResetWithCandidate(byte);
            break;
    }
}

void UartProtocol_Process(void) {
    while(rx_tail != rx_head) { uint8_t byte = rx_ring[rx_tail]; rx_tail = (uint16_t)((rx_tail + 1U) % RX_RING_SIZE); ParserConsume(byte); }
    TxKick();
}
