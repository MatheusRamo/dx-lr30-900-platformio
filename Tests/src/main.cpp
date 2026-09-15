#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Crc24Q.h>
#include <Rtcm3Parser.h>
#include <RtcmRadioProtocol.h>
#include <UartFrame.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
struct UartCapture{int calls=0;uint8_t type=0;uint16_t seq=0,length=0;uint8_t payload[260]{};};
static void uartCb(void*c,const UartFrame::Frame&f){auto*x=(UartCapture*)c;x->calls++;x->type=f.type;x->seq=f.sequence;x->length=f.length;memcpy(x->payload,f.payload,f.length);}
struct RtcmCapture{int valid=0,invalid=0;uint16_t type=0;};
static void rtcmCb(void*c,const uint8_t*,size_t,uint16_t t,bool ok){auto*x=(RtcmCapture*)c;if(ok)x->valid++;else x->invalid++;x->type=t;}
static size_t makeRtcm(uint8_t*out,uint16_t type,size_t payloadLength=20){memset(out,0,payloadLength+6);out[0]=0xD3;out[1]=(payloadLength>>8)&3;out[2]=payloadLength;out[3]=type>>4;out[4]=(type&15)<<4;uint32_t c=crc24q(out,payloadLength+3);out[payloadLength+3]=c>>16;out[payloadLength+4]=c>>8;out[payloadLength+5]=c;return payloadLength+6;}
static void testCrcs(){const uint8_t v[]="123456789";CHECK(UartFrame::crc16(v,9)==0x29B1);CHECK(crc24q(v,9)==0xCDE703);}
static void testUart(){uint8_t special[]={0x00,0x0A,0x0D,0xA5,0x5A,0xFF},encoded[64];size_t n=0;CHECK(UartFrame::encode(0x10,0xBEEF,special,sizeof(special),encoded,sizeof(encoded),n));UartCapture c;UartFrame::Decoder d(uartCb,&c);d.feed(encoded,3);d.feed(encoded+3,n-3);CHECK(c.calls==1&&c.type==0x10&&c.seq==0xBEEF&&c.length==sizeof(special)&&memcmp(c.payload,special,sizeof(special))==0);encoded[n-1]^=1;d.feed(encoded,n);CHECK(c.calls==1);}
static void testRtcmSplitAndCorruption(){uint8_t frame[128];size_t n=makeRtcm(frame,1077,80);RtcmCapture c;Rtcm3Parser p(rtcmCb,&c);p.feed(frame,7);p.feed(frame+7,31);p.feed(frame+38,n-38);CHECK(c.valid==1&&c.type==1077);frame[20]^=1;p.feed(frame,n);CHECK(c.invalid==1);n=makeRtcm(frame,1005,20);p.feed(frame,n);CHECK(c.valid==2&&c.type==1005);}
static void testRadioStreamAcrossPackets(){uint8_t frame[400],a[249],b[249];size_t n=makeRtcm(frame,1127,300),wa=0,wb=0;RtcmRadio::Header h1{42,100,0,0},h2{42,101,0,0};CHECK(RtcmRadio::encode(a,sizeof(a),h1,frame,180,wa));CHECK(RtcmRadio::encode(b,sizeof(b),h2,frame+180,n-180,wb));RtcmRadio::Header h;const uint8_t*s;RtcmCapture c;Rtcm3Parser p(rtcmCb,&c);CHECK(RtcmRadio::decode(a,wa,h,s));p.feed(s,h.length);CHECK(c.valid==0);CHECK(RtcmRadio::decode(b,wb,h,s));p.feed(s,h.length);CHECK(c.valid==1&&c.type==1127);}
static void testSequence(){RtcmRadio::SequenceTracker t;uint16_t lost=0;CHECK(t.observe(1,65535,lost)==RtcmRadio::SequenceResult::FIRST);CHECK(t.observe(1,0,lost)==RtcmRadio::SequenceResult::ACCEPT);CHECK(t.observe(1,0,lost)==RtcmRadio::SequenceResult::DUPLICATE);CHECK(t.observe(1,3,lost)==RtcmRadio::SequenceResult::LOSS&&lost==2);CHECK(t.observe(2,9,lost)==RtcmRadio::SequenceResult::SESSION_CHANGED);}
static void testPacketLossRecovery(){uint8_t partial[128],valid[64];size_t a=makeRtcm(partial,1077,80),b=makeRtcm(valid,1005,20);(void)a;RtcmCapture c;Rtcm3Parser p(rtcmCb,&c);p.feed(partial,30);p.reset();p.feed(valid,b);CHECK(c.valid==1&&c.type==1005);}
void runControlTests();
void runGnssTests();
void runNtripTests();
int main(){testCrcs();testUart();testRtcmSplitAndCorruption();testRadioStreamAcrossPackets();testSequence();testPacketLossRecovery();runControlTests();runGnssTests();runNtripTests();puts("All protocol tests passed");return 0;}
