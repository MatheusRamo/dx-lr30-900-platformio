#pragma once
#include <Arduino.h>
#include <WiFi.h>

struct RoverNtripConfig {
    char ssid[33]{}, wifiPass[64]{}, host[128]{}, mount[128]{}, user[96]{}, password[96]{};
    uint16_t port=2101;
    bool sendGga=true;
};

// The worker owns the socket, including DNS, connect and GGA writes. It never
// touches the GNSS UART or web server. Queues carry POD copies, never Strings.
class DirectNtrip {
public:
    enum Kind { CONNECTING, CONNECTED, DATA, FAILURE, GGA_SENT };
    struct Event { uint32_t generation=0, at=0; Kind kind=FAILURE; uint16_t length=0; uint8_t data[512]{}; };
    struct Gga { uint32_t at=0; char text[192]{}; };
    uint32_t generation=0;
    bool begin() {
        commands=xQueueCreate(1,sizeof(Command)); events=xQueueCreate(12,sizeof(Event)); ggas=xQueueCreate(1,sizeof(Gga));
        if (commands && events && ggas && xTaskCreate(runTask,"rover-ntrip",8192,this,1,nullptr)==pdPASS) return true;
        if(commands)vQueueDelete(commands); if(events)vQueueDelete(events); if(ggas)vQueueDelete(ggas);
        commands=events=ggas=nullptr; return false;
    }
    void configure(const RoverNtripConfig &config, bool enabled) {
        Command c{}; c.config=config; c.enabled=enabled; c.generation=++generation;
        if (commands) xQueueOverwrite(commands,&c);
    }
    bool receive(Event &event) { return events && xQueueReceive(events,&event,0)==pdTRUE; }
    void gga(const char *text,uint32_t at) {
        Gga g{}; g.at=at; strlcpy(g.text,text,sizeof(g.text)); if (ggas) xQueueOverwrite(ggas,&g);
    }
private:
    struct Command { RoverNtripConfig config; uint32_t generation=0; bool enabled=false; };
    QueueHandle_t commands=nullptr,events=nullptr,ggas=nullptr;
    static String basic64(const String &in) {
        const char *t="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        String out; out.reserve((in.length()+2)/3*4);
        for (size_t i=0;i<in.length();i+=3) {
            uint32_t v=(uint8_t)in[i]<<16; bool b=i+1<in.length(),c=i+2<in.length();
            if(b)v|=(uint8_t)in[i+1]<<8; if(c)v|=(uint8_t)in[i+2];
            out+=t[(v>>18)&63]; out+=t[(v>>12)&63]; out+=b?t[(v>>6)&63]:'='; out+=c?t[v&63]:'=';
        }
        return out;
    }
    bool emit(Event &e,Kind kind,uint32_t generation,const char *reason=nullptr) {
        e.kind=kind; e.generation=generation; e.at=millis();
        if(reason) { strlcpy((char*)e.data,reason,sizeof(e.data)); e.length=strlen(reason); }
        return xQueueSend(events,&e,pdMS_TO_TICKS(100))==pdTRUE;
    }
    static void runTask(void *p) { static_cast<DirectNtrip*>(p)->run(); }
    void run() {
        Command active{},next{}; Event e{}; WiFiClient client;
        bool connected=false; uint32_t retry=0,lastData=0,lastGga=0; unsigned failures=0;
        for (;;) {
            if (xQueueReceive(commands,&next,0)==pdTRUE) {
                active=next; client.stop(); connected=false; retry=millis()+1000; failures=0;
            }
            const uint32_t now=millis();
            if (!active.enabled || WiFi.status()!=WL_CONNECTED) {
                if (connected) { client.stop(); connected=false; emit(e,FAILURE,active.generation,"WIFI_DISCONNECTED"); }
                vTaskDelay(pdMS_TO_TICKS(20)); continue;
            }
            const char *error=nullptr;
            if (!connected && (int32_t)(now-retry)>=0) {
                emit(e,CONNECTING,active.generation);
                if (!client.connect(active.config.host,active.config.port,3000)) error="TCP_CONNECT_FAILED";
                else {
                    client.setTimeout(2); client.setNoDelay(true);
                    String request="GET /"+String(active.config.mount)+" HTTP/1.0\r\nHost: "+String(active.config.host)+":"+String(active.config.port)+"\r\nUser-Agent: NTRIP ESP32-RTK-ROVER/1.0\r\nAccept: */*\r\n";
                    if (*active.config.user) request+="Authorization: Basic "+basic64(String(active.config.user)+":"+active.config.password)+"\r\n";
                    request+="Connection: close\r\n\r\n";
                    if (client.print(request)!=request.length()) error="REQUEST_WRITE_FAILED";
                    else if (!emit(e,CONNECTED,active.generation)) error="QUEUE_OVERFLOW";
                    else { connected=true; lastData=millis(); lastGga=millis()-5000; }
                }
            }
            if (connected && !error) {
                int available=client.available();
                if (available>0) {
                    int n=client.read(e.data,available>(int)sizeof(e.data)?sizeof(e.data):available);
                    if(n>0) { e.length=n; lastData=millis(); if(!emit(e,DATA,active.generation)) error="QUEUE_OVERFLOW"; else failures=0; }
                }
                if (!client.connected() && !client.available()) error="CASTER_DISCONNECTED";
                if (millis()-lastData>10000) error="NO_DATA_10S";
                if (!error && active.config.sendGga && millis()-lastGga>=5000) {
                    Gga g{};
                    if(xQueuePeek(ggas,&g,0)==pdTRUE && *g.text && millis()-g.at<3000) {
                        String line(g.text); line.trim(); line+="\r\n";
                        if(client.print(line)!=line.length()) error="GGA_WRITE_FAILED";
                        else { lastGga=millis(); emit(e,GGA_SENT,active.generation); }
                    }
                }
            }
            if(error) {
                client.stop(); connected=false; emit(e,FAILURE,active.generation,error);
                static const uint32_t delays[]={1000,2000,5000,10000,30000};
                retry=millis()+delays[failures<4?failures:4]; if(failures<4)++failures;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
};
