#include "UartFrame.h"
#include <string.h>
namespace UartFrame {
static uint16_t update(uint16_t crc,const uint8_t*d,size_t n){for(size_t i=0;i<n;++i){crc^=(uint16_t)d[i]<<8;for(uint8_t b=0;b<8;++b)crc=(crc&0x8000U)?(uint16_t)((crc<<1)^0x1021U):(uint16_t)(crc<<1);}return crc;}
uint16_t crc16(const uint8_t*d,size_t n){return update(0xFFFFU,d,n);}
bool encode(uint8_t type,uint16_t seq,const uint8_t*p,uint16_t n,uint8_t*out,size_t cap,size_t&w){w=0;if(!out||(!p&&n)||n>MAX_PAYLOAD||cap<n+10)return false;out[0]=0xA5;out[1]=0x5A;out[2]=1;out[3]=type;out[4]=seq;out[5]=seq>>8;out[6]=n;out[7]=n>>8;if(n)memcpy(out+8,p,n);uint16_t c=crc16(out+2,n+6);out[n+8]=c;out[n+9]=c>>8;w=n+10;return true;}
void Decoder::reset(){state_=S0;hp_=0;pp_=length_=0;}
void Decoder::feed(const uint8_t*d,size_t n){for(size_t i=0;i<n;++i)consume(d[i]);}
void Decoder::consume(uint8_t b){switch(state_){case S0:if(b==0xA5)state_=S1;break;case S1:if(b==0x5A){state_=H;hp_=0;}else state_=b==0xA5?S1:S0;break;case H:header_[hp_++]=b;if(hp_==6){length_=header_[4]|(header_[5]<<8);pp_=0;if(header_[0]!=1||length_>MAX_PAYLOAD)reset();else state_=length_?P:C0;}break;case P:payload_[pp_++]=b;if(pp_==length_)state_=C0;break;case C0:received_=b;state_=C1;break;case C1:{received_|=(uint16_t)b<<8;uint16_t c=update(update(0xFFFFU,header_,6),payload_,length_);if(c==received_&&callback_){Frame f{header_[1],(uint16_t)(header_[2]|(header_[3]<<8)),length_,payload_};callback_(context_,f);}state_=b==0xA5?S1:S0;break;}}}
}
