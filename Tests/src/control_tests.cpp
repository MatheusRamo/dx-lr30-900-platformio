#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <ControlWire.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#endif
using namespace RadioControl;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
struct Flight {bool base;Message m;uint8_t profile;uint32_t end;bool deliver;};
struct Sim {
    Machine base,rover;uint32_t now=100;std::vector<Flight> flights;
    int dropKind=0,dropCount=0;bool block=false,failTx=false,dropCommitted=false;
    bool duplicate=false;uint32_t readyDelay=80;
    uint8_t bp=0,rp=0;uint32_t br=0,rr=0;bool bb=false,rb=false;
    Sim(){base.begin(true,0,123,now);rover.begin(false,0,456,now);}
    void step(){
        now+=10;
        for(size_t i=0;i<flights.size();){
            auto f=flights[i];if(!due(now,f.end)){i++;continue;}
            bool listening=f.base?(!rb&&rp==f.profile&&due(now,rr)):(!bb&&bp==f.profile&&due(now,br));
            if(f.base)bb=false;else rb=false;
            (f.base?base:rover).txDone(now,!failTx);
            if(f.deliver&&listening&&!failTx){(f.base?rover:base).receive(f.m,now);if(duplicate)(f.base?rover:base).receive(f.m,now);}
            flights.erase(flights.begin()+i);
        }
        base.tick(now);rover.tick(now);
        send(true);send(false);
    }
    void send(bool b){
        Machine&m=b?base:rover;bool&busy=b?bb:rb;uint8_t&p=b?bp:rp;uint32_t&ready=b?br:rr;
        if(busy)return;
        if(p!=m.profile()){p=m.profile();ready=now+readyDelay;}
        if(!due(now,ready))return;
        Message msg;if(!m.take(now,msg))return;
        bool delivery=!block;
        if(dropCommitted&&msg.kind==BEACON&&!(msg.flags&TRIAL)&&msg.active==4)delivery=false;
        if(msg.kind==dropKind&&dropCount){delivery=false;if(dropCount>0)--dropCount;}
        flights.push_back({b,msg,p,now+airtimeMs(p,(uint8_t)packetSize(msg.kind))+20,delivery});busy=true;
    }
    void run(uint32_t ms){uint32_t until=now+ms;while(!due(now,until))step();}
    void rebootBase(){CHECK(!bb);base.begin(true,0,789,now);}
    void rebootRover(){CHECK(!rb);rover.begin(false,0,987,now);}
    void settleIdle(){while(bb||rb)step();}
};
static void successAllProfiles(){
    for(uint8_t p=0;p<PROFILE_COUNT;p++){
        Sim s;s.run(2500);CHECK(s.rover.heardBase());CHECK(s.rover.request(p,100+p,s.now));
        s.run(15000);CHECK(s.base.active()==p);CHECK(s.rover.active()==p);
        CHECK(!s.rover.pending());CHECK(s.rover.result()==2);
    }
}
static void droppedMessages(){
    for(int kind=BEACON;kind<=PROBE;kind++){
        Sim s;s.run(2000);s.dropKind=kind;s.dropCount=1;
        CHECK(s.rover.request(5,700+kind,s.now));s.run(55000);
        CHECK(s.base.active()==5&&s.rover.active()==5);CHECK(s.rover.result()==2);
    }
    Sim s;s.run(2000);s.dropKind=PROBE;s.dropCount=-1;
    CHECK(s.rover.request(4,99,s.now));s.run(15000);
    CHECK(s.base.active()==0);CHECK(s.rover.result()!=2);
    s.dropCount=0;s.run(44000);CHECK(s.base.active()==4&&s.rover.active()==4&&s.rover.result()==2);
}
static void rebootRecovery(){
    for(int which=0;which<3;which++){
        Sim s;s.run(2000);s.rover.request(4,50,s.now);s.run(12000);s.settleIdle();
        CHECK(s.base.active()==4);
        if(which!=1)s.rebootBase();if(which!=0)s.rebootRover();
        s.run(42000);CHECK(s.base.active()==s.rover.active());CHECK(s.base.profile()==s.rover.profile());
    }
}
static void noLinkAndInvalid(){
    Sim s;s.run(2000);CHECK(!s.rover.request(255,1,s.now));CHECK(!s.rover.request(3,0,s.now));
    CHECK(s.rover.request(3,22,s.now));CHECK(!s.rover.request(4,23,s.now));s.block=true;s.run(65000);
    CHECK(s.rover.result()==3&&!s.rover.pending());CHECK(s.base.active()==0);
    s.block=false;s.run(35000);CHECK(s.rover.active()==0);
    Message bad;bad.kind=REQUEST;bad.boot=999;bad.target=5;bad.transaction=1;
    s.base.receive(bad,s.now);CHECK(s.base.active()==0);
}
static void confirmationAndRecoveryEdges(){
    Sim s;s.run(2000);s.dropCommitted=true;s.rover.request(4,800,s.now);s.run(14000);
    CHECK(s.base.active()==4);CHECK(s.rover.result()!=2); // TX done is not remote success.
    s.dropCommitted=false;s.run(42000);CHECK(s.rover.result()==2&&s.rover.active()==4);
    Sim duplicate;duplicate.duplicate=true;duplicate.readyDelay=1200;
    duplicate.run(2000);duplicate.rover.request(6,990,duplicate.now);duplicate.run(55000);
    CHECK(duplicate.base.active()==6&&duplicate.rover.active()==6&&duplicate.rover.result()==2);
    Sim timeout;timeout.run(2000);timeout.rover.request(3,771,timeout.now);timeout.failTx=true;timeout.run(10000);
    CHECK(timeout.base.active()==0&&timeout.rover.result()!=2);timeout.failTx=false;timeout.run(45000);
    CHECK(timeout.base.active()==3&&timeout.rover.result()==2);
    Sim sameRescue;sameRescue.run(2000);sameRescue.rover.request(RESCUE_PROFILE,92,sameRescue.now);sameRescue.run(12000);
    sameRescue.block=true;sameRescue.run(6000);CHECK(sameRescue.rover.rescue());
    sameRescue.block=false;sameRescue.run(3000);CHECK(!sameRescue.rover.rescue());
    Sim wrap;wrap.now=0xFFFFF000;wrap.base.begin(true,0,123,wrap.now);wrap.rover.begin(false,0,456,wrap.now);
    wrap.br=wrap.rr=wrap.now;wrap.run(2000);wrap.rover.request(4,222,wrap.now);wrap.run(15000);
    CHECK(wrap.rover.result()==2&&wrap.base.active()==4);
    // Reboot at multiple stages of a negotiation; stale commands must not strand peers.
    for(uint32_t delay=100;delay<7000;delay+=700){
        Sim reboot;reboot.run(2000);reboot.rover.request(4,334,reboot.now);reboot.run(delay);reboot.settleIdle();
        reboot.rebootBase();reboot.run(70000);CHECK(reboot.base.active()==reboot.rover.active());
    }
}
static bool testSign(void*key,const uint8_t*p,size_t n,uint8_t*out){
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg;BCRYPT_HASH_HANDLE hash;uint8_t digest[32];
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)!=0)return false;
    bool ok=BCryptCreateHash(alg,&hash,nullptr,0,(PUCHAR)key,32,0)==0;
    if(ok){ok=BCryptHashData(hash,(PUCHAR)p,(ULONG)n,0)==0&&BCryptFinishHash(hash,digest,32,0)==0;BCryptDestroyHash(hash);}
    BCryptCloseAlgorithmProvider(alg,0);if(ok)memcpy(out,digest,16);return ok;
#else
    // Wire framing test only on non-Windows; production uses mbedTLS SHA256 HMAC.
    memset(out,0,16);for(size_t i=0;i<n;i++)out[i%16]^=p[i]^((uint8_t*)key)[i%32];return true;
#endif
}
static void wireControlWire(){
    uint8_t key[32]={1,2,3},wrong[32]={9},packet[BEACON_SIZE];Message m;m.kind=BEACON;m.boot=7;m.challenge=9;m.telemetry[QUEUE_DROPS]=1234;
    CHECK(encode(m,packet,testSign,key));Message decoded;
    CHECK(decode(packet,sizeof(packet),decoded,testSign,key));CHECK(decoded.boot==7&&decoded.challenge==9);
    CHECK(decoded.telemetry[QUEUE_DROPS]==1234);
    CHECK(decode(packet,sizeof(packet),decoded,testSign,wrong));
    packet[10]^=1;CHECK(decode(packet,sizeof(packet),decoded,testSign,key));packet[10]^=1;
    CHECK(!decode(packet,sizeof(packet)-1,decoded,testSign,key));
}
void runControlTests(){
    CHECK(airtimeMs(0,249)==34);CHECK(airtimeMs(6,249)>500);CHECK(profileId("RTK_FAST")==0);
    successAllProfiles();droppedMessages();rebootRecovery();noLinkAndInvalid();confirmationAndRecoveryEdges();wireControlWire();
    puts("All control protocol tests passed");
}
