#pragma once
#include <Arduino.h>
#include <Lr30Link.h>
#include "ControlWire.h"

// Owns all UART configuration and TX transactions; one packet in flight.
class RadioControlLink {
public:
    explicit RadioControlLink(Lr30Link &link):link_(link){}
    RadioControl::Machine machine;
    uint32_t txOk=0,txErrors=0,configErrors=0,authErrors=0;
    uint32_t localTelemetry[RadioControl::TELEMETRY_FIELDS]={};
    RadioControl::Message baseTelemetry;
    uint32_t baseTelemetryAt=0;
    bool hasBaseTelemetry=false;
    int16_t controlRssi=0;int8_t controlSnr=0;
    void begin(bool base,uint8_t profile) {
        base_=base;machine.begin(base,profile,randomId(),millis());
    }
    bool keyed()const{return true;}
    bool ready()const{return configured_&&configuredProfile_==machine.profile();}
    bool busy()const{return busy_;}
    uint8_t configuredProfile()const{return configuredProfile_;}
    bool request(uint8_t profile){return machine.request(profile,randomId(),millis());}
    bool dataAllowed()const{return ready()&&!busy_&&machine.dataAllowed(millis());}
    bool sendData(const uint8_t*p,uint8_t n){if(!dataAllowed())return false;return send(p,n,false);}
    void service() {
        uint32_t now=millis();
        if(busy_&&RadioControl::due(now,txDeadline_)) {
            busy_=false;txErrors++;if(controlTx_)machine.txDone(now,false);
            configured_=false;configPending_=false;nextConfig_=now;
        }
        machine.tick(now);
        if(busy_)return;
        if(ready()&&RadioControl::due(now,nextHealth_)){
            configured_=false;configPending_=true;checking_=true;requestedProfile_=configuredProfile_;
            statusSeq_=sequence_++;link_.send(Lr30Link::STATUS_REQ,statusSeq_);nextConfig_=now+1000;return;
        }
        if(!ready()) {
            if(!RadioControl::due(now,nextConfig_))return;
            configured_=false;configPending_=true;checking_=false;requestedProfile_=machine.profile();
            configSeq_=sequence_++;link_.setRadioConfig(configSeq_,config(requestedProfile_));
            nextConfig_=now+1000;return;
        }
        {RadioControl::Message m;if(machine.take(now,m)){
            if(base_&&m.kind==RadioControl::BEACON)memcpy(m.telemetry,localTelemetry,sizeof(localTelemetry));
            uint8_t packet[RadioControl::BEACON_SIZE];
            if(!RadioControl::encode(m,packet,nullptr,nullptr)||!send(packet,RadioControl::packetSize(m.kind),true))machine.txDone(now,false);
        }}
    }
    // Returns true for modem/control frames, false for RF application data.
    bool onFrame(const Lr30Link::Frame &f) {
        if(f.type==Lr30Link::CONFIG_RESULT) {
            if(configPending_&&!checking_&&f.sequence==configSeq_) {
                if(f.length==1&&f.payload[0]==0){checking_=true;statusSeq_=sequence_++;link_.send(Lr30Link::STATUS_REQ,statusSeq_);}
                else{configErrors++;nextConfig_=millis()+150;}
            }return true;
        }
        if(f.type==Lr30Link::STATUS_RESP) {
            if(configPending_&&checking_&&f.sequence==statusSeq_){
                const auto c=config(requestedProfile_);
                bool ok=f.length==15&&f.payload[0]==0&&RadioControl::get32(f.payload+1)==c.frequency&&
                    RadioControl::get32(f.payload+5)==c.bandwidth&&f.payload[9]==c.sf&&f.payload[10]==c.cr&&
                    (int8_t)f.payload[11]==c.power&&(f.payload[12]|(f.payload[13]<<8))==c.preamble&&f.payload[14]==1;
                configured_=ok;configuredProfile_=requestedProfile_;configPending_=false;
                if(ok)nextHealth_=millis()+10000;
                if(!ok){configErrors++;nextConfig_=millis()+150;}
            }return true;
        }
        if(f.type==Lr30Link::TX_RESULT) {
            if(busy_&&f.sequence==txSeq_){bool ok=f.length==1&&f.payload[0]==0;
                busy_=false;if(ok)txOk++;else txErrors++;if(controlTx_)machine.txDone(millis(),ok);}
            return true;
        }
        if(f.type==Lr30Link::RX_PACKET&&f.length>=6&&f.payload[4]=='R'&&f.payload[5]=='C') {
            RadioControl::Message m;
            if(f.length==uint16_t(f.payload[3])+4&&RadioControl::decode(f.payload+4,f.payload[3],m,nullptr,nullptr)){
                bool newer=!hasBaseTelemetry||m.boot!=baseTelemetry.boot||(int32_t)(m.challenge-baseTelemetry.challenge)>0;
                if(!base_&&m.kind==RadioControl::BEACON&&newer){
                    baseTelemetry=m;baseTelemetryAt=millis();hasBaseTelemetry=true;
                    controlRssi=(int16_t)(f.payload[0]|(f.payload[1]<<8));controlSnr=(int8_t)f.payload[2];
                }
                machine.receive(m,millis());
            }
            else authErrors++;
            return true;
        }
        return f.type!=Lr30Link::RX_PACKET;
    }
    static Lr30Link::RadioConfig config(uint8_t id){
        const auto&p=RadioControl::profiles[id<RadioControl::PROFILE_COUNT?id:0];
        Lr30Link::RadioConfig c;c.sf=p.sf;c.bandwidth=p.bandwidth;c.cr=p.cr;c.preamble=p.preamble;return c;
    }
private:
    static uint32_t randomId(){uint32_t v=esp_random();return v?v:1;}
    bool send(const uint8_t*p,uint8_t n,bool control){
        if(busy_||!ready())return false;txSeq_=sequence_++;
        if(!link_.send(Lr30Link::TX_PACKET,txSeq_,p,n)){txErrors++;return false;}
        busy_=true;controlTx_=control;txDeadline_=millis()+RadioControl::airtimeMs(machine.profile(),n)+700;return true;
    }
    Lr30Link &link_;
    bool base_=false,busy_=false,controlTx_=false,configured_=false,configPending_=false,checking_=false;
    uint8_t configuredProfile_=255,requestedProfile_=0;
    uint16_t sequence_=1,configSeq_=0,statusSeq_=0,txSeq_=0;
    uint32_t nextConfig_=0,txDeadline_=0,nextHealth_=0;
};
