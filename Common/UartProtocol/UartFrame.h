#pragma once
#include <stddef.h>
#include <stdint.h>

namespace UartFrame {
constexpr size_t MAX_PAYLOAD=260, MAX_FRAME=MAX_PAYLOAD+10;
struct Frame {uint8_t type;uint16_t sequence;uint16_t length;const uint8_t*payload;};
uint16_t crc16(const uint8_t*data,size_t length);
bool encode(uint8_t type,uint16_t sequence,const uint8_t*payload,uint16_t length,uint8_t*out,size_t capacity,size_t&written);
class Decoder {
public:
    using Callback=void(*)(void*,const Frame&);Decoder(Callback callback=nullptr,void*context=nullptr):callback_(callback),context_(context){}
    void feed(const uint8_t*data,size_t length);void reset();
private:
    enum State{S0,S1,H,P,C0,C1};void consume(uint8_t byte);
    Callback callback_;void*context_;State state_=S0;uint8_t header_[6],payload_[MAX_PAYLOAD],hp_=0;uint16_t pp_=0,length_=0,received_=0;
};
}
