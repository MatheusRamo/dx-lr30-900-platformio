#pragma once

static bool configValid(const RoverNtripConfig &c) {
    if (!*c.ssid || !*c.host || !*c.mount || !c.port) return false;
    if (*c.wifiPass && (strlen(c.wifiPass)<8 || strlen(c.wifiPass)>63)) return false;
    for(const char *p=c.host;*p;++p) if(!isalnum((unsigned char)*p) && *p!='.' && *p!='-') return false;
    for(const char *p=c.mount;*p;++p) if((unsigned char)*p<=32 || (unsigned char)*p>=127 || *p=='?' || *p=='#') return false;
    for(const char *p=c.user;*p;++p) if((unsigned char)*p<32 || *p==':') return false;
    return true;
}
static uint32_t lastWifiAttempt=0;
static void resetCorrectionStats() {
    stats.rtcmFrames=stats.rtcmCrcOk=stats.rtcmCrcErrors=stats.rtcmRate=0;
    stats.rtcmBytes=0; gnssWritten=0; lastRtcmAt=0; maxRtcmGap=0; lastRtcmType=0;
    sequenceTracker.reset(); correctionSelectedAt=millis();
}
static void applyCorrectionSource() {
    corrections.select(corrections.source); resetCorrectionStats(); ntripResponse.reset();
    ntripBodyBytes=0; ntripRate=0; ntripLastData=0; ntripLastGga=0; ntripConnectedAt=0;
    ntripError=""; ntripHttpStatus=0; lastWifiAttempt=millis();
    bool enabled=corrections.source==CorrectionInput::NTRIP;
    if (ntripWorkerReady) directNtrip.configure(ntripConfig,enabled);
    if(enabled) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.setSleep(false);
        ntripState="WIFI_CONNECTING";
        WiFi.setAutoReconnect(true); WiFi.begin(ntripConfig.ssid,ntripConfig.wifiPass);
    } else {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false,false);
        WiFi.mode(WIFI_AP);
        ntripState="OFF";
    }
    if (!startAccessPoint()) Serial.println("ERR WIFI_AP_AFTER_SOURCE");
}
static void restartNtrip(const char *reason) {
    ntripFailures++; ntripError=reason; ntripState="RECONNECTING";
    ntripConnectedAt=0; ntripResponse.reset(); corrections.reset(CorrectionInput::NTRIP);
    directNtrip.configure(ntripConfig,true);
}
static void serviceDirectNtrip() {
    DirectNtrip::Event event{};
    unsigned budget=6;
    while(budget-- && directNtrip.receive(event)) {
        if(corrections.source!=CorrectionInput::NTRIP || event.generation!=directNtrip.generation) continue;
        switch(event.kind) {
        case DirectNtrip::CONNECTING:
            ntripAttempts++; ntripState="CONNECTING"; break;
        case DirectNtrip::CONNECTED:
            ntripResponse.reset(); corrections.reset(CorrectionInput::NTRIP);
            ntripState="WAIT_RESPONSE"; ntripConnectedAt=ntripValidAt=millis(); break;
        case DirectNtrip::FAILURE:
            ntripFailures++; ntripError=(const char*)event.data; ntripState="RECONNECTING";
            ntripConnectedAt=0; ntripResponse.reset(); corrections.reset(CorrectionInput::NTRIP); break;
        case DirectNtrip::GGA_SENT:
            ntripGgaSent++; ntripLastGga=event.at; break;
        case DirectNtrip::DATA:
            if(millis()-event.at>500) { ntripQueueDrops++; restartNtrip("BACKLOG_DISCARDED"); break; }
            for(size_t i=0;i<event.length;++i) {
                auto result=ntripResponse.feed(event.data[i]);
                ntripHttpStatus=ntripResponse.code;
                if(result==NtripResponse::ERROR || result==NtripResponse::END) {
                    restartNtrip(result==NtripResponse::END?"STREAM_ENDED":ntripResponse.reason); break;
                }
                if(ntripResponse.streaming()) { ntripState="STREAM"; ntripError=""; }
                if(result==NtripResponse::BYTE) {
                    ntripBodyBytes++; ntripLastData=event.at;
                    corrections.feed(CorrectionInput::NTRIP,event.data+i,1);
                }
            }
            break;
        }
    }
    if(corrections.source==CorrectionInput::NTRIP) {
        if(WiFi.status()!=WL_CONNECTED) {
            ntripState="WIFI_CONNECTING";
            if(millis()-lastWifiAttempt>=15000) { lastWifiAttempt=millis(); WiFi.reconnect(); }
        }
        if(ntripState=="WAIT_RESPONSE" && millis()-ntripConnectedAt>8000) restartNtrip("RESPONSE_TIMEOUT");
        if(ntripState=="STREAM" && millis()-ntripValidAt>15000) restartNtrip("NO_VALID_RTCM_15S");
        if(WiFi.status()==WL_CONNECTED && ntripState=="WIFI_CONNECTING") ntripState="WAIT_CASTER";
    }
    static uint32_t at=0; static uint64_t bytes=0;
    if(millis()-at>=1000) {
        ntripRate=(ntripBodyBytes>=bytes?ntripBodyBytes-bytes:ntripBodyBytes)*1000/(millis()-at);
        bytes=ntripBodyBytes; at=millis();
    }
}
static bool saveNtripConfig() {
    Preferences p; if(!p.begin("rover-ntrip",false)) return false;
    bool ok=p.putBytes("config",&ntripConfig,sizeof(ntripConfig))==sizeof(ntripConfig);
    ok=(p.putBool("direct",corrections.source==CorrectionInput::NTRIP)>0)&&ok; p.end(); return ok;
}
static bool readField(const char *name,char *dest,size_t size,bool keepEmpty=false) {
    if(!web.hasArg(name)) return false;
    String value=web.arg(name); if(value.length()>=size || strlen(value.c_str())!=value.length()) return false;
    if(!keepEmpty || !value.isEmpty()) value.toCharArray(dest,size);
    return true;
}
static void setupNtrip() {
    Preferences p; p.begin("rover-ntrip",true);
    if(p.getBytesLength("config")==sizeof(ntripConfig)) p.getBytes("config",&ntripConfig,sizeof(ntripConfig));
    // Always bound persisted strings before validating/using them.
    ntripConfig.ssid[32]=ntripConfig.wifiPass[63]=ntripConfig.host[127]=ntripConfig.mount[127]=ntripConfig.user[95]=ntripConfig.password[95]=0;
    if(p.getBool("direct",false) && configValid(ntripConfig)) {
        ntripWorkerReady=directNtrip.begin();
        if(ntripWorkerReady) corrections.source=CorrectionInput::NTRIP;
    }
    p.end(); applyCorrectionSource();
    web.on("/corrections/config",HTTP_GET,[] {
        String s="{\"source\":"+jsonText(corrections.name());
        s+=",\"ssid\":"+jsonText(ntripConfig.ssid)+",\"host\":"+jsonText(ntripConfig.host);
        s+=",\"port\":"+String(ntripConfig.port)+",\"mount\":"+jsonText(ntripConfig.mount)+",\"user\":"+jsonText(ntripConfig.user);
        s+=",\"gga\":"+String(ntripConfig.sendGga?"true":"false");
        s+=",\"wifiPasswordSaved\":"+String(*ntripConfig.wifiPass?"true":"false")+",\"ntripPasswordSaved\":"+String(*ntripConfig.password?"true":"false")+"}";
        web.sendHeader("Cache-Control","no-store"); web.send(200,"application/json",s);
    });
    web.on("/corrections/config",HTTP_POST,[] {
        RoverNtripConfig next=ntripConfig;
        bool ok=readField("ssid",next.ssid,sizeof(next.ssid)) && readField("wifiPass",next.wifiPass,sizeof(next.wifiPass),true)
            && readField("host",next.host,sizeof(next.host)) && readField("mount",next.mount,sizeof(next.mount))
            && readField("user",next.user,sizeof(next.user)) && readField("password",next.password,sizeof(next.password),true);
        String port=web.arg("port"); for(size_t i=0;i<port.length();++i) if(!isdigit(port[i])) ok=false;
        long number=port.toInt(); if(number<1 || number>65535 || port.length()>5) ok=false; next.port=(uint16_t)number;
        next.sendGga=web.arg("gga")=="1";
        if(web.arg("clearWifi")=="1") *next.wifiPass=0;
        if(web.arg("clearPassword")=="1") *next.password=0;
        while(*next.mount=='/') memmove(next.mount,next.mount+1,strlen(next.mount));
        if(!ok || !configValid(next)) { web.send(400,"text/plain","Confira SSID, senha Wi-Fi (8 a 63 caracteres ou rede aberta), host sem http://, porta e mountpoint sem espacos."); return; }
        auto previous=ntripConfig; ntripConfig=next;
        if(!saveNtripConfig()) { ntripConfig=previous; web.send(500,"text/plain","Falha ao salvar no ESP32."); return; }
        web.send(200,"text/plain","Configuracao NTRIP salva no rover.");
        if(corrections.source==CorrectionInput::NTRIP) applyCorrectionsPending=true;
    });
    web.on("/corrections/source",HTTP_POST,[] {
        String source=web.arg("source");
        if(source!="LORA" && source!="NTRIP") { web.send(400,"text/plain","Origem invalida."); return; }
        if(source=="NTRIP" && !configValid(ntripConfig)) { web.send(409,"text/plain","Salve uma configuracao NTRIP valida primeiro."); return; }
        if(source=="NTRIP" && !ntripWorkerReady) ntripWorkerReady=directNtrip.begin();
        if(source=="NTRIP" && !ntripWorkerReady) { web.send(503,"text/plain","Memoria insuficiente para iniciar o cliente NTRIP."); return; }
        if(radioControl.machine.pending()) { web.send(409,"text/plain","Aguarde a negociacao do perfil de radio."); return; }
        auto previous=corrections.source; corrections.source=source=="NTRIP"?CorrectionInput::NTRIP:CorrectionInput::LORA;
        if(!saveNtripConfig()) { corrections.source=previous; web.send(500,"text/plain","Falha ao salvar origem."); return; }
        web.send(200,"text/plain","Origem salva. Aguarde novas correcoes; confira RTCM e FIX na leitura atual.");
        applyCorrectionsPending=true;
    });
}
