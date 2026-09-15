#include "Lr30Link.h"
#include <string.h>

static uint16_t crc16Update(uint16_t crc, const uint8_t *data, size_t length) {
    for(size_t i=0;i<length;++i) { crc ^= static_cast<uint16_t>(data[i]) << 8; for(uint8_t b=0;b<8;++b) crc=(crc&0x8000U)?static_cast<uint16_t>((crc<<1)^0x1021U):static_cast<uint16_t>(crc<<1); }
    return crc;
}
Lr30Link::Lr30Link(Stream &stream):stream_(stream){}
void Lr30Link::setHandler(Handler h, void *c){handler_=h;context_=c;}
uint16_t Lr30Link::crc16(const uint8_t *data,size_t length){return crc16Update(0xFFFFU,data,length);}
void Lr30Link::reset(uint8_t c){state_=(c==0xA5)?SYNC1:SYNC0;headerPos_=0;payloadPos_=payloadLength_=0;}
bool Lr30Link::send(uint8_t type,uint16_t seq,const uint8_t *payload,uint16_t length){
    if(length>MAX_PAYLOAD||(!payload&&length))return false; uint8_t h[8]={0xA5,0x5A,1,type,(uint8_t)seq,(uint8_t)(seq>>8),(uint8_t)length,(uint8_t)(length>>8)};
    uint16_t crc=crc16Update(0xFFFFU,h+2,6);crc=crc16Update(crc,payload,length);
    if(stream_.write(h,8)!=8)return false;if(length&&stream_.write(payload,length)!=length)return false;uint8_t c[2]={(uint8_t)crc,(uint8_t)(crc>>8)};return stream_.write(c,2)==2;
}
bool Lr30Link::sendHello(uint16_t s){return send(HELLO_REQ,s);}
bool Lr30Link::setRadioConfig(uint16_t s,const RadioConfig &c){uint8_t p[14];auto w=[](uint8_t*x,uint32_t v){x[0]=v;x[1]=v>>8;x[2]=v>>16;x[3]=v>>24;};w(p,c.frequency);w(p+4,c.bandwidth);p[8]=c.sf;p[9]=c.cr;p[10]=(uint8_t)c.power;p[11]=c.preamble;p[12]=c.preamble>>8;p[13]=c.crc;return send(CONFIG_SET,s,p,sizeof(p));}
void Lr30Link::poll(){if(state_!=SYNC0&&millis()-lastByteAt_>100){partialTimeouts++;reset();}while(stream_.available()>0)consume(static_cast<uint8_t>(stream_.read()));}
void Lr30Link::consume(uint8_t b){
    lastByteAt_=millis();
    switch(state_){case SYNC0:if(b==0xA5)state_=SYNC1;break;case SYNC1:if(b==0x5A){state_=HEADER;headerPos_=0;}else reset(b);break;
    case HEADER:header_[headerPos_++]=b;if(headerPos_==6){payloadLength_=header_[4]|(header_[5]<<8);payloadPos_=0;if(header_[0]!=1||payloadLength_>MAX_PAYLOAD){malformedFrames++;reset(b);}else state_=payloadLength_?PAYLOAD:CRC0;}break;
    case PAYLOAD:payload_[payloadPos_++]=b;if(payloadPos_==payloadLength_)state_=CRC0;break;case CRC0:receivedCrc_=b;state_=CRC1;break;
    case CRC1:{receivedCrc_|=(uint16_t)b<<8;uint16_t c=crc16Update(0xFFFFU,header_,6);c=crc16Update(c,payload_,payloadLength_);if(c==receivedCrc_&&handler_){Frame f{header_[1],static_cast<uint16_t>(header_[2]|(header_[3]<<8)),payloadLength_,payload_};handler_(context_,f);}else if(c!=receivedCrc_)crcErrors++;reset(b);break;}}
}
