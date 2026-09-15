#include <Arduino.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Lr30Link.h>
#include <Rtcm3Parser.h>
#include <RtcmRadioProtocol.h>
#include <RadioControlLink.h>

HardwareSerial LoRaSerial(2);
BluetoothSerial SerialBT;
Preferences preferences;
WiFiClient ntrip;
Lr30Link lr30(LoRaSerial);
RadioControlLink radioControl(lr30);

constexpr uint8_t LORA_RX_PIN=16, LORA_TX_PIN=17;
constexpr uint32_t LORA_BAUD=115200, MAX_QUEUE_AGE_MS=2000;
constexpr size_t RADIO_QUEUE_SIZE=12, RTCM_TYPE_SLOTS=32, FILTER_MAX=24;

struct Config {
    String wifiSsid,wifiPass,ntripHost,ntripMount,ntripUser,ntripPass;
    uint16_t ntripPort=2101; String profile="RTK_FAST",filter="ALL";
} config;
struct TypeStats { uint16_t type=0; uint32_t frames=0,bytes=0; } typeStats[RTCM_TYPE_SLOTS];
struct Stats {
    uint64_t ntripBytes=0,radioBytes=0; uint32_t rtcmValid=0,rtcmCrcErrors=0,filtered=0;
    uint32_t radioPackets=0,radioErrors=0,queueDrops=0,staleDrops=0;
    uint32_t ntripRate=0,radioRate=0;
} stats;
struct QueueItem { uint8_t data[1029]; uint16_t length=0,offset=0; uint32_t created=0; } radioQueue[RADIO_QUEUE_SIZE];

uint8_t queueHead=0,queueTail=0,queueCount=0;
uint16_t sessionId=0,radioSequence=0;
uint16_t filterTypes[FILTER_MAX];size_t filterCount=0;bool filterAll=true;
String usbLine,btLine;

enum class NtripState { OFF, WAIT_STATUS, WAIT_HEADERS, STREAM };
NtripState ntripState=NtripState::OFF;char ntripLine[256];size_t ntripLineUsed=0;
uint32_t nextNtripAttempt=0;uint8_t ntripBackoffIndex=0;const uint32_t backoffs[]={1000,2000,5000,10000,30000};
Rtcm3Parser rtcmParser;
struct NtripConnectJob {String host,request;uint16_t port;uint32_t generation;WiFiClient client;bool ok=false;};
QueueHandle_t ntripConnectResults=nullptr;
bool ntripConnectBusy=false;uint32_t ntripGeneration=0;
static void ntripConnectTask(void*arg){
    auto*job=static_cast<NtripConnectJob*>(arg);
    job->ok=job->client.connect(job->host.c_str(),job->port,3000);
    if(job->ok)job->ok=job->client.print(job->request)==job->request.length();
    xQueueSend(ntripConnectResults,&job,portMAX_DELAY);vTaskDelete(nullptr);
}

static void logLine(const String&s){Serial.println(s);if(SerialBT.hasClient())SerialBT.println(s);}
static String basic64(const String&in){
    static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";String out;out.reserve(((in.length()+2)/3)*4);
    for(size_t i=0;i<in.length();i+=3){uint32_t v=(uint8_t)in[i]<<16;bool b=i+1<in.length(),c=i+2<in.length();if(b)v|=(uint8_t)in[i+1]<<8;if(c)v|=(uint8_t)in[i+2];out+=table[(v>>18)&63];out+=table[(v>>12)&63];out+=b?table[(v>>6)&63]:'=';out+=c?table[v&63]:'=';}return out;
}
static void loadConfig(){
    preferences.begin("rtk-base",true);config.wifiSsid=preferences.getString("ssid","");config.wifiPass=preferences.getString("wpass","");
    config.ntripHost=preferences.getString("host","");config.ntripPort=preferences.getUShort("port",2101);config.ntripMount=preferences.getString("mount","");
    config.ntripUser=preferences.getString("user","");config.ntripPass=preferences.getString("npass","");config.profile=preferences.getString("profile","RTK_FAST");config.filter=preferences.getString("filter","ALL");preferences.end();
}
static void saveConfig(){preferences.begin("rtk-base",false);preferences.putString("ssid",config.wifiSsid);preferences.putString("wpass",config.wifiPass);preferences.putString("host",config.ntripHost);preferences.putUShort("port",config.ntripPort);preferences.putString("mount",config.ntripMount);preferences.putString("user",config.ntripUser);preferences.putString("npass",config.ntripPass);preferences.putString("profile",config.profile);preferences.putString("filter",config.filter);preferences.end();}
static bool parseFilter(const String&text){
    if(text=="ALL"){filterAll=true;filterCount=0;return true;}filterAll=false;filterCount=0;int start=0;
    while(start<(int)text.length()&&filterCount<FILTER_MAX){int comma=text.indexOf(',',start);if(comma<0)comma=text.length();String part=text.substring(start,comma);long v=part.toInt();if(v<1||v>4095)return false;filterTypes[filterCount++]=(uint16_t)v;start=comma+1;}
    return filterCount>0&&start>=(int)text.length();
}
static bool allowed(uint16_t type){if(filterAll)return true;for(size_t i=0;i<filterCount;++i)if(filterTypes[i]==type)return true;return false;}
static void countType(uint16_t type,size_t bytes){TypeStats*slot=nullptr;for(auto&s:typeStats){if(s.type==type){slot=&s;break;}if(!s.type&&!slot)slot=&s;}if(slot){slot->type=type;slot->frames++;slot->bytes+=bytes;}}
static void newRadioSession(){sessionId=(uint16_t)esp_random();if(!sessionId)sessionId=1;radioSequence=0;}
static void clearBacklog(bool stale){queueHead=queueTail=queueCount=0;newRadioSession();if(stale)stats.staleDrops++;}
static void onRtcm(void*,const uint8_t*frame,size_t length,uint16_t type,bool valid){
    if(!valid){stats.rtcmCrcErrors++;return;}stats.rtcmValid++;countType(type,length);if(!allowed(type)){stats.filtered++;return;}
    if(length>sizeof(radioQueue[0].data))return;
    if(queueCount==RADIO_QUEUE_SIZE){queueTail=(queueTail+1)%RADIO_QUEUE_SIZE;queueCount--;stats.queueDrops++;newRadioSession();}
    auto&item=radioQueue[queueHead];memcpy(item.data,frame,length);item.length=length;item.offset=0;item.created=millis();
    queueHead=(queueHead+1)%RADIO_QUEUE_SIZE;queueCount++;
}
static void processLr30(void*,const Lr30Link::Frame&f){
    radioControl.onFrame(f);
}
static void serviceRadio(){
    auto*t=radioControl.localTelemetry;
    t[RadioControl::UPTIME]=millis();t[RadioControl::NTRIP_RATE]=stats.ntripRate;t[RadioControl::RADIO_RATE]=stats.radioRate;
    t[RadioControl::QUEUE]=queueCount;t[RadioControl::QUEUE_DROPS]=stats.queueDrops;t[RadioControl::STALE_DROPS]=stats.staleDrops;
    t[RadioControl::RTCM_VALID]=stats.rtcmValid;t[RadioControl::RTCM_CRC_ERRORS]=stats.rtcmCrcErrors;t[RadioControl::RTCM_FILTERED]=stats.filtered;
    t[RadioControl::TX_OK]=radioControl.txOk;t[RadioControl::TX_ERRORS]=radioControl.txErrors;
    t[RadioControl::CONFIG_ERRORS]=radioControl.configErrors;t[RadioControl::AUTH_ERRORS]=radioControl.authErrors;
    t[RadioControl::WIFI_RSSI]=(uint32_t)(int32_t)(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0);
    t[RadioControl::NTRIP_STATE]=(uint32_t)ntripState;
    radioControl.service();
    static uint8_t lastProfile=255;
    if(lastProfile!=radioControl.machine.profile()){lastProfile=radioControl.machine.profile();clearBacklog(false);}
    config.profile=RadioControl::profiles[radioControl.machine.active()].name;
    stats.radioPackets=radioControl.txOk;stats.radioErrors=radioControl.txErrors+radioControl.configErrors;
    while(queueCount&&millis()-radioQueue[queueTail].created>MAX_QUEUE_AGE_MS){
        queueTail=(queueTail+1)%RADIO_QUEUE_SIZE;queueCount--;stats.staleDrops++;newRadioSession();
    }
    if(!queueCount||!radioControl.dataAllowed())return;
    auto&item=radioQueue[queueTail];size_t n=item.length-item.offset;if(n>RtcmRadio::STREAM_CHUNK_MAX)n=RtcmRadio::STREAM_CHUNK_MAX;
    uint8_t packet[RtcmRadio::PACKET_MAX];size_t written=0;
    RtcmRadio::Header h{sessionId,radioSequence,static_cast<uint8_t>(n),static_cast<uint8_t>(item.offset==0?1:0)};
    if(RtcmRadio::encode(packet,sizeof(packet),h,item.data+item.offset,n,written)&&radioControl.sendData(packet,written)){
        radioSequence++;stats.radioBytes+=written;item.offset+=n;
        if(item.offset==item.length){queueTail=(queueTail+1)%RADIO_QUEUE_SIZE;queueCount--;}
    }
}
static void ntripFailed(){ntripGeneration++;ntrip.stop();ntripState=NtripState::OFF;rtcmParser.reset();clearBacklog(false);nextNtripAttempt=millis()+backoffs[ntripBackoffIndex];if(ntripBackoffIndex<4)ntripBackoffIndex++;}
static void connectNtrip(){
    if(ntripConnectBusy||!ntripConnectResults||WiFi.status()!=WL_CONNECTED||config.ntripHost.isEmpty()||config.ntripMount.isEmpty())return;
    auto*job=new NtripConnectJob;job->host=config.ntripHost;job->port=config.ntripPort;job->generation=ntripGeneration;
    String auth=basic64(config.ntripUser+":"+config.ntripPass);
    job->request="GET /"+config.ntripMount+" HTTP/1.0\r\nUser-Agent: NTRIP ESP32-RTK-BASE/2.0\r\nAccept: */*\r\nAuthorization: Basic "+auth+"\r\nConnection: close\r\n\r\n";
    ntripConnectBusy=true;
    if(xTaskCreate(ntripConnectTask,"ntrip-connect",6144,job,1,nullptr)!=pdPASS){ntripConnectBusy=false;delete job;ntripFailed();}
}
static void handleNtripLine(){
    ntripLine[ntripLineUsed]='\0';if(ntripState==NtripState::WAIT_STATUS){String s(ntripLine);if(s.startsWith("ICY 200")){ntripState=NtripState::STREAM;ntripBackoffIndex=0;}else if(s.indexOf(" 200 ")>=0)ntripState=NtripState::WAIT_HEADERS;else ntripFailed();}
    else if(ntripState==NtripState::WAIT_HEADERS&&ntripLineUsed==0){ntripState=NtripState::STREAM;ntripBackoffIndex=0;}ntripLineUsed=0;
}
static void serviceNtrip(){
    NtripConnectJob*job=nullptr;
    if(ntripConnectResults&&xQueueReceive(ntripConnectResults,&job,0)==pdTRUE){
        ntripConnectBusy=false;
        if(job->generation==ntripGeneration){
            if(job->ok&&WiFi.status()==WL_CONNECTED){ntrip=job->client;ntripState=NtripState::WAIT_STATUS;ntripLineUsed=0;}
            else ntripFailed();
        }
        delete job;
    }
    if(WiFi.status()!=WL_CONNECTED){if(ntripState!=NtripState::OFF)ntripFailed();return;}if(ntripState==NtripState::OFF){if((int32_t)(millis()-nextNtripAttempt)>=0)connectNtrip();return;}
    unsigned budget=512;while(budget--&&ntrip.available()){uint8_t b=ntrip.read();if(ntripState==NtripState::STREAM){stats.ntripBytes++;rtcmParser.feed(b);}else if(b=='\n'){if(ntripLineUsed&&ntripLine[ntripLineUsed-1]=='\r')ntripLineUsed--;handleNtripLine();}else if(ntripLineUsed<sizeof(ntripLine)-1)ntripLine[ntripLineUsed++]=(char)b;else ntripFailed();}
    if(!ntrip.connected()&&!ntrip.available())ntripFailed();
}
static void showStatus(){
    logLine("========== RTK BASE ==========");logLine(String("WiFi: ")+(WiFi.status()==WL_CONNECTED?"OK, RSSI "+String(WiFi.RSSI())+" dBm":"OFF"));
    logLine(String("NTRIP: ")+(ntripState==NtripState::STREAM?"OK":"OFF")+", Mount "+config.ntripMount);
    logLine("RTCM: RX "+String(stats.ntripRate)+" B/s, Valid "+String(stats.rtcmValid)+", CRC Error "+String(stats.rtcmCrcErrors)+", Filtered "+String(stats.filtered));
    logLine("Radio: "+config.profile+", Queue "+String(queueCount)+", TX "+String(stats.radioRate)+" B/s, Packets "+String(stats.radioPackets)+", Errors "+String(stats.radioErrors));
    logLine("Drops: queue "+String(stats.queueDrops)+", stale "+String(stats.staleDrops));logLine("UART STM32: 115200");logLine("==============================");
}
static void showRtcm(){logLine("TYPE   FRAMES   BYTES");for(const auto&s:typeStats)if(s.type)logLine(String(s.type)+"   "+String(s.frames)+"   "+String(s.bytes));}
static void reconnect(){ntripGeneration++;ntrip.stop();ntripState=NtripState::OFF;rtcmParser.reset();clearBacklog(false);nextNtripAttempt=0;ntripBackoffIndex=0;WiFi.disconnect(false,false);if(!config.wifiSsid.isEmpty())WiFi.begin(config.wifiSsid.c_str(),config.wifiPass.c_str());}
static void command(String c){
    c.trim();if(c=="STATUS"){showStatus();return;}if(c=="CONFIG"){logLine("SSID="+config.wifiSsid);logLine("WIFI_PASS="+String(config.wifiPass.isEmpty()?"<vazio>":"********"));logLine("NTRIP_HOST="+config.ntripHost);logLine("NTRIP_PORT="+String(config.ntripPort));logLine("NTRIP_MOUNT="+config.ntripMount);logLine("NTRIP_USER="+config.ntripUser);logLine("NTRIP_PASS="+String(config.ntripPass.isEmpty()?"<vazio>":"********"));logLine("LORA_PROFILE="+config.profile);logLine("RTCM_FILTER="+config.filter);return;}
    if(c=="SAVE"){saveConfig();logLine("OK SAVED");return;}if(c=="RECONNECT"){reconnect();return;}if(c=="RTCM"){showRtcm();return;}if(c=="RADIO"){showStatus();return;}
    if(c=="STATS_RESET"){stats=Stats{};radioControl.txOk=radioControl.txErrors=radioControl.configErrors=0;for(auto&s:typeStats)s=TypeStats{};logLine("OK STATS_RESET");return;}if(c=="STATS"){showStatus();showRtcm();return;}
    auto set=[&](const char*k,String&v,bool secret=false){if(c.startsWith(String(k)+"=")){v=c.substring(strlen(k)+1);logLine(String("OK ")+k+(secret?"=<oculto>":"="+v));return true;}return false;};
    if(set("WIFI_SSID",config.wifiSsid)||set("WIFI_PASS",config.wifiPass,true)||set("NTRIP_HOST",config.ntripHost)||set("NTRIP_MOUNT",config.ntripMount)||set("NTRIP_USER",config.ntripUser)||set("NTRIP_PASS",config.ntripPass,true))return;
    if(c.startsWith("NTRIP_PORT=")){long p=c.substring(11).toInt();if(p>0&&p<65536){config.ntripPort=p;logLine("OK NTRIP_PORT");}else logLine("ERR PORT");return;}
    if(c.startsWith("LORA_PROFILE=")){logLine("Use a pagina do rover para trocar ambas as pontas.");return;}
    if(c.startsWith("RTCM_FILTER=")){String f=c.substring(12);if(parseFilter(f)){config.filter=f;clearBacklog(false);logLine("OK RTCM_FILTER="+f);}else logLine("ERR FILTER");return;}logLine("ERR UNKNOWN");
}
static void terminal(Stream&s,String&line){while(s.available()){char c=s.read();if(c=='\r'||c=='\n'){if(!line.isEmpty()){
    line.trim();command(line);line="";
}}else if(line.length()<200)line+=c;else line="";}}
static void updateRates(){static uint32_t at=0;static uint64_t nb=0,rb=0;if(millis()-at>=1000){uint32_t dt=millis()-at;stats.ntripRate=(stats.ntripBytes>=nb?stats.ntripBytes-nb:stats.ntripBytes)*1000/dt;stats.radioRate=(stats.radioBytes>=rb?stats.radioBytes-rb:stats.radioBytes)*1000/dt;nb=stats.ntripBytes;rb=stats.radioBytes;at=millis();}}

void setup(){
    Serial.begin(115200);LoRaSerial.setRxBufferSize(2048);LoRaSerial.begin(LORA_BAUD,SERIAL_8N1,LORA_RX_PIN,LORA_TX_PIN);SerialBT.begin("RTK-BASE");loadConfig();parseFilter(config.filter);
    ntripConnectResults=xQueueCreate(1,sizeof(NtripConnectJob*));
    WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.persistent(false);if(!config.wifiSsid.isEmpty())WiFi.begin(config.wifiSsid.c_str(),config.wifiPass.c_str());
    rtcmParser.setCallback(onRtcm,nullptr);lr30.setHandler(processLr30,nullptr);newRadioSession();
    int id=RadioControl::profileId(config.profile.c_str());radioControl.begin(true,id<0?0:id);logLine("RTK-BASE pronto; controle de perfil sem pareamento.");
}
void loop(){lr30.poll();serviceRadio();terminal(Serial,usbLine);terminal(SerialBT,btLine);serviceNtrip();lr30.poll();serviceRadio();updateRates();}
