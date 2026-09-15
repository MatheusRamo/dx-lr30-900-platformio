#pragma once
#include "ControlMachine.h"
#include <stddef.h>
#include <string.h>
namespace RadioControl {
constexpr size_t CONTROL_SIZE=40, SIGNED_SIZE=24;
constexpr size_t BEACON_SIZE=CONTROL_SIZE+4*TELEMETRY_FIELDS;
constexpr size_t packetSize(uint8_t kind){return kind==BEACON?BEACON_SIZE:CONTROL_SIZE;}
using Signer=bool (*)(void*,const uint8_t*,size_t,uint8_t*);
inline void put32(uint8_t*p,uint32_t v){for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
inline uint32_t get32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline bool encode(const Message&m,uint8_t*out,Signer sign,void*ctx){
    size_t n=packetSize(m.kind),signedSize=n-16;
    memset(out,0,n);out[0]='R';out[1]='C';out[2]=2;out[3]=PROFILE_VERSION;
    out[4]=m.kind;out[5]=m.active;out[6]=m.target;out[7]=m.flags;
    put32(out+8,m.boot);put32(out+12,m.challenge);put32(out+16,m.transaction);
    if(m.kind==BEACON)for(size_t i=0;i<TELEMETRY_FIELDS;i++)put32(out+SIGNED_SIZE+i*4,m.telemetry[i]);
    // Authentication is intentionally disabled for field tests. The final
    // 16 bytes remain reserved so the wire format stays easy to extend.
    (void)sign;(void)ctx;return true;
}
inline bool decode(const uint8_t*p,size_t n,Message&m,Signer sign,void*ctx){
    if(n<CONTROL_SIZE||n!=packetSize(p[4])||p[0]!='R'||p[1]!='C'||p[2]!=2||p[3]!=PROFILE_VERSION||
       p[4]<BEACON||p[4]>PROBE||p[5]>=PROFILE_COUNT||p[6]>=PROFILE_COUNT||
       (p[7]&~(TRIAL|RESCUE))||get32(p+20)!=0)return false;
    size_t signedSize=n-16;(void)signedSize;(void)sign;(void)ctx;
    m.kind=p[4];m.active=p[5];m.target=p[6];m.flags=p[7];
    m.boot=get32(p+8);m.challenge=get32(p+12);m.transaction=get32(p+16);
    for(size_t i=0;i<TELEMETRY_FIELDS;i++)m.telemetry[i]=m.kind==BEACON?get32(p+SIGNED_SIZE+i*4):0;
    return true;
}
}
