#pragma once
#include "RadioProfiles.h"

namespace RadioControl {
enum Kind : uint8_t { BEACON=1, REQUEST=2, OFFER=3, PROBE=4 };
enum Flags : uint8_t { TRIAL=1, RESCUE=2 };
enum TelemetryField { UPTIME, NTRIP_RATE, RADIO_RATE, QUEUE, QUEUE_DROPS, STALE_DROPS,
    RTCM_VALID, RTCM_CRC_ERRORS, RTCM_FILTERED, TX_OK, TX_ERRORS, CONFIG_ERRORS, AUTH_ERRORS,
    WIFI_RSSI, NTRIP_STATE, TELEMETRY_FIELDS };
struct Message {
    uint8_t kind=0, active=0, target=0, flags=0;
    uint32_t boot=0, challenge=0, transaction=0;
    uint32_t telemetry[TELEMETRY_FIELDS]={};
};
// Transport independent. Field-test control intentionally has no authentication.
// One base is the authority; a lost final beacon is reconciled by later beacons.
class Machine {
public:
    void begin(bool base,uint8_t initial,uint32_t boot,uint32_t now) {
        *this=Machine{}; base_=base; active_=current_=initial<PROFILE_COUNT?initial:0;
        boot_=boot; nextBeacon_=now+200; nextRescue_=now+30000; lastHeard_=now;quietUntil_=now;
    }
    bool request(uint8_t target,uint32_t transaction,uint32_t now) {
        if(base_||pending_||target>=PROFILE_COUNT||!transaction) return false;
        wanted_=target; requestId_=transaction; pending_=true; requestUntil_=now+60000;
        result_=1; return true;
    }
    void tick(uint32_t now) {
        if(pending_&&due(now,requestUntil_)) { pending_=false; result_=3;if(!sending_)queued_=false; }
        if(base_) {
            if(trial_&&due(now,trialUntil_)&&!sending_) {
                trial_=false; queued_=false; tune(active_,now); nextBeacon_=now+150;
            }
            if(!trial_&&!sending_&&!queued_&&due(now,nextRescue_)) {
                rescue_=true; rescueUntil_=now+3500; nextRescue_=now+30000;
                tune(RESCUE_PROFILE,now); nextBeacon_=now+150;
            }
            if(rescue_&&!trial_&&!sending_&&due(now,rescueUntil_)) {
                rescue_=false; tune(active_,now); nextBeacon_=now+150;
            }
            if(!sending_&&!queued_&&due(now,nextBeacon_)&&due(now,quietUntil_)) {
                Message m; m.kind=BEACON; m.boot=boot_; m.challenge=++challenge_;
                m.active=active_; m.target=trial_?candidate_:active_;
                m.flags=(trial_?TRIAL:0)|(rescue_?RESCUE:0); m.transaction=transaction_;
                enqueue(m,now);
            }
        } else if(!sending_&&!queued_&&!rescue_&&due(now,lastHeard_+5000)) {
            rescue_=true; tune(RESCUE_PROFILE,now);
        }
    }
    bool take(uint32_t now,Message &m) {
        if(!queued_||sending_||!due(now,sendAt_)) return false;
        m=out_; sent_=m; queued_=false; sending_=true; return true;
    }
    void txDone(uint32_t now,bool success) {
        if(!sending_) return;
        sending_=false;
        if(base_&&sent_.kind==BEACON) {
            quietUntil_=now+airtimeMs(current_,40)+180;
            nextBeacon_=now+(trial_||rescue_?650:1500);
        }
        if(base_&&sent_.kind==OFFER) {
            if(success) { rescue_=false; trial_=true; trialUntil_=now+8000;
                tune(candidate_,now); nextBeacon_=now+200; }
            else { trial_=false; tune(active_,now); nextBeacon_=now+200; }
        }
    }
    void receive(const Message &m,uint32_t now) {
        if(m.active>=PROFILE_COUNT||m.target>=PROFILE_COUNT||!m.boot) return;
        if(base_) {
            if(m.boot!=boot_) return;
            if(m.kind==REQUEST&&!trial_&&!queued_&&!sending_&&
               m.challenge==challenge_&&m.transaction&& !due(now,quietUntil_)) {
                // Consume this one-time challenge before emitting the offer.
                ++challenge_; candidate_=m.target; transaction_=m.transaction;
                Message reply=m; reply.kind=OFFER; reply.active=active_; reply.flags=0;
                enqueue(reply,now+40);
            } else if(m.kind==PROBE&&trial_&&m.transaction==transaction_&&
                      m.target==candidate_&&m.challenge==challenge_) {
                active_=candidate_; trial_=false; rescue_=false;
                nextRescue_=now+30000; nextBeacon_=now+80;
            }
            return;
        }
        if(m.kind==OFFER&&pending_&&m.boot==peerBoot_&&m.challenge==peerChallenge_&&m.transaction==requestId_&&m.target==wanted_) {
            rescue_=false; tune(m.target,now); lastHeard_=now; return;
        }
        if(m.kind!=BEACON) return;
        // A challenge increases on each beacon. Reject duplicates/replayed beacons
        // in this boot; boot changes are reconciled through authenticated discovery.
        if(m.boot==peerBoot_&&(int32_t)(m.challenge-peerChallenge_)<=0) return;
        peerBoot_=m.boot; peerChallenge_=m.challenge; lastHeard_=now; active_=m.active;
        if(m.flags&TRIAL) {
            if(pending_&&m.transaction==requestId_&&m.target==wanted_&&current_==m.target) {
                Message reply=m; reply.kind=PROBE; enqueue(reply,now+40);
            }
            return;
        }
        if(!(m.flags&RESCUE)&&current_==m.active)rescue_=false;
        if(pending_&&m.transaction==requestId_&&m.active==wanted_&&!(m.flags&RESCUE)&&current_==wanted_) {
            pending_=false; result_=2;
        }
        if(pending_) {
            Message reply=m; reply.kind=REQUEST; reply.target=wanted_;
            reply.transaction=requestId_; reply.flags=0; enqueue(reply,now+40);
        } else if((m.flags&RESCUE)||current_!=m.active) {
            rescue_=false; tune(m.active,now);
        }
    }
    uint8_t profile() const { return current_; }
    uint8_t active() const { return active_; }
    bool pending() const { return pending_; }
    bool rescue() const { return rescue_; }
    bool trial() const { return trial_; }
    uint8_t result() const { return result_; } // 0 idle, 1 pending, 2 RF confirmed, 3 expired
    uint32_t lastHeard() const { return lastHeard_; }
    bool heardBase() const { return peerBoot_!=0; }
    bool dataAllowed(uint32_t now) const {
        return base_&&!trial_&&!rescue_&&!sending_&&!queued_&&due(now,quietUntil_)&&!due(now,nextBeacon_);
    }
private:
    void tune(uint8_t profile,uint32_t now) { current_=profile; quietUntil_=now+150; }
    void enqueue(const Message &m,uint32_t at) { if(!queued_&&!sending_) {out_=m;queued_=true;sendAt_=at;} }
    bool base_=false,pending_=false,trial_=false,rescue_=false,queued_=false,sending_=false;
    uint8_t active_=0,current_=0,candidate_=0,wanted_=0,result_=0;
    uint32_t boot_=0,challenge_=0,transaction_=0,peerBoot_=0,peerChallenge_=0,requestId_=0;
    uint32_t nextBeacon_=0,nextRescue_=0,rescueUntil_=0,trialUntil_=0,quietUntil_=0;
    uint32_t lastHeard_=0,requestUntil_=0,sendAt_=0;
    Message out_,sent_;
};
}
