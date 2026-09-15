#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class Lr30Link {
public:
    static constexpr uint8_t HELLO_REQ=0x01, HELLO_RESP=0x81, CONFIG_SET=0x02, CONFIG_RESULT=0x82;
    static constexpr uint8_t STATUS_REQ=0x03, STATUS_RESP=0x83, TX_PACKET=0x10, TX_RESULT=0x90, RX_PACKET=0x91, ERROR_MSG=0x7F;
    static constexpr size_t MAX_PAYLOAD = 260;
    struct Frame { uint8_t type; uint16_t sequence; uint16_t length; const uint8_t *payload; };
    struct RadioConfig { uint32_t frequency=915000000, bandwidth=500000; uint8_t sf=5, cr=5; int8_t power=22; uint16_t preamble=8; bool crc=true; };
    using Handler = void (*)(void *context, const Frame &frame);

    explicit Lr30Link(Stream &stream);
    void setHandler(Handler handler, void *context);
    void poll();
    uint32_t crcErrors=0,malformedFrames=0,partialTimeouts=0;
    bool send(uint8_t type, uint16_t sequence, const uint8_t *payload=nullptr, uint16_t length=0);
    bool sendHello(uint16_t sequence);
    bool setRadioConfig(uint16_t sequence, const RadioConfig &config);
    static uint16_t crc16(const uint8_t *data, size_t length);
private:
    enum State { SYNC0, SYNC1, HEADER, PAYLOAD, CRC0, CRC1 };
    void consume(uint8_t byte); void reset(uint8_t candidate=0);
    Stream &stream_; Handler handler_=nullptr; void *context_=nullptr; State state_=SYNC0;
    uint8_t header_[6], payload_[MAX_PAYLOAD], headerPos_=0; uint16_t payloadPos_=0, payloadLength_=0, receivedCrc_=0;
    uint32_t lastByteAt_=0;
};
