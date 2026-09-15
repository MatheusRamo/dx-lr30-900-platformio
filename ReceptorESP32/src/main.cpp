#include <Arduino.h>
#include <Preferences.h>
#include <BluetoothSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <GnssMonitor.h>
#include <Lr30Link.h>
#include <Rtcm3Parser.h>
#include <RtcmRadioProtocol.h>
#include <RadioControlLink.h>
#include <WiFi.h>
#include <WebServer.h>
#include "FieldPage.h"
#include "CorrectionInput.h"
#include "DirectNtrip.h"
#include "NtripResponse.h"

HardwareSerial LoRaSerial(2), GNSSSerial(1);
BluetoothSerial SerialBT;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Lr30Link lr30(LoRaSerial);
RadioControlLink radioControl(lr30);
WebServer web(80);
String apPassword;
bool displayReady = false;
uint32_t lastRtcmAt = 0, lastGgaAt = 0, lastGstAt = 0, lastGsaAt = 0, lastRmcAt = 0, lastRadioAt = 0;
uint32_t roverBoot = 0, nmeaDrops = 0, maxRtcmGap = 0;
uint16_t lastRtcmType = 0;
uint64_t gnssWritten = 0;
struct NmeaLine
{
    uint16_t length;
    uint8_t data[192];
};
QueueHandle_t nmeaQueue = nullptr;
static void bluetoothWriter(void *)
{
    NmeaLine line;
    for (;;)
    {
        if (xQueueReceive(nmeaQueue, &line, portMAX_DELAY) == pdTRUE && SerialBT.hasClient())
            SerialBT.write(line.data, line.length);
    }
}
CorrectionInput corrections;
DirectNtrip directNtrip;
RoverNtripConfig ntripConfig;
NtripResponse ntripResponse;
bool ntripWorkerReady=false, applyCorrectionsPending=false;
String ntripState="OFF", ntripError;
uint32_t ntripAttempts=0, ntripFailures=0, ntripGgaSent=0, ntripQueueDrops=0;
uint32_t ntripLastData=0, ntripLastGga=0, ntripConnectedAt=0, correctionSelectedAt=0;
uint64_t ntripBodyBytes=0;
uint32_t ntripRate=0;
int ntripHttpStatus=0;
uint32_t ntripValidAt=0;
RtcmRadio::SequenceTracker sequenceTracker;
GnssMonitor gnss;

constexpr uint8_t LORA_RX_PIN = 16, LORA_TX_PIN = 17, GNSS_RX_PIN = 25, GNSS_TX_PIN = 26, OLED_SDA = 21, OLED_SCL = 22;
constexpr uint32_t UART_BAUD = 115200;
struct Stats
{
    uint32_t radioPackets = 0, lost = 0, duplicates = 0, invalidRadio = 0;
    int16_t rssi = 0;
    int8_t snr = 0;
    uint32_t rtcmFrames = 0, rtcmCrcOk = 0, rtcmCrcErrors = 0;
    uint64_t rtcmBytes = 0;
    uint32_t rtcmRate = 0;
} stats;
String usbLine;

static void onRtcm(void *, const uint8_t *frame, size_t length, uint16_t type, bool valid)
{
    if (!valid)
    {
        stats.rtcmCrcErrors++;
        return;
    }
    stats.rtcmFrames++;
    stats.rtcmCrcOk++;
    stats.rtcmBytes += length;
    if (lastRtcmAt && millis() - lastRtcmAt > maxRtcmGap)
        maxRtcmGap = millis() - lastRtcmAt;
    lastRtcmAt = millis();
    lastRtcmType = type;
    if(corrections.source==CorrectionInput::NTRIP) ntripValidAt=millis();
    gnssWritten += GNSSSerial.write(frame, length);
}
static void onLr30(void *, const Lr30Link::Frame &f)
{
    if (radioControl.onFrame(f))
        return;
    if (corrections.source != CorrectionInput::LORA)
        return;
    if (f.type != Lr30Link::RX_PACKET || f.length < 4)
        return;
    int16_t rssi = (int16_t)(f.payload[0] | (f.payload[1] << 8));
    int8_t snr = (int8_t)f.payload[2];
    uint8_t length = f.payload[3];
    if (f.length != (uint16_t)(length + 4))
    {
        stats.invalidRadio++;
        return;
    }
    RtcmRadio::Header header;
    const uint8_t *stream = nullptr;
    if (!RtcmRadio::decode(f.payload + 4, length, header, stream))
    {
        stats.invalidRadio++;
        return;
    }
    uint16_t lost = 0;
    auto result = sequenceTracker.observe(header.session, header.sequence, lost);
    if (result == RtcmRadio::SequenceResult::DUPLICATE)
    {
        stats.duplicates++;
        return;
    }
    if (result == RtcmRadio::SequenceResult::LOSS)
    {
        stats.lost += lost;
        corrections.reset(CorrectionInput::LORA);
    }
    else if (result == RtcmRadio::SequenceResult::SESSION_CHANGED)
        corrections.reset(CorrectionInput::LORA);
    if (header.flags & 1)
        corrections.reset(CorrectionInput::LORA); // First fragment of a complete RTCM message.
    stats.radioPackets++;
    lastRadioAt = millis();
    stats.rssi = rssi;
    stats.snr = snr;
    corrections.feed(CorrectionInput::LORA, stream, header.length);
}
static float lossPercent()
{
    uint64_t total = (uint64_t)stats.radioPackets + stats.lost;
    return total ? 100.0f * stats.lost / total : 0;
}
static void showStatus()
{
    const auto &g = gnss.status();
    Serial.println("========== RTK ROVER ==========");
    Serial.printf("Corrections: %s, NTRIP: %s, Error: %s\n", corrections.name(), ntripState.c_str(), ntripError.c_str());
    Serial.printf("Profile: %s, Radio ready: %d, Pending: %d, Rescue: %d\n", RadioControl::profiles[radioControl.machine.active()].name, radioControl.ready(), radioControl.machine.pending(), radioControl.machine.rescue());
    Serial.printf("Radio: RX %lu, Lost %lu, Loss %.2f%%, RSSI %d, SNR %d\n", stats.radioPackets, stats.lost, lossPercent(), stats.rssi, stats.snr);
    Serial.printf("RTCM: Frames %lu, CRC OK %lu, CRC Error %lu, RX %lu B/s\n", stats.rtcmFrames, stats.rtcmCrcOk, stats.rtcmCrcErrors, stats.rtcmRate);
    Serial.printf("GNSS: %s, Satellites %u, HDOP %.2f, Age %.1f s\n", GnssMonitor::fixText(g.quality), g.satellites, g.hdop, g.differentialAge);
    Serial.println("===============================");
}
static void updateDisplay()
{
    if (!displayReady)
        return;
    static uint32_t last = 0;
    if (millis() - last < 500)
        return;
    last = millis();
    const auto &g = gnss.status();
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(lastGgaAt && millis() - lastGgaAt < 3000 ? GnssMonitor::fixText(g.quality) : "GNSS --");
    display.setCursor(82, 0);
    display.print("SAT");
    display.print(g.satellites);
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    display.setCursor(0, 13);
    display.print("HDOP ");
    display.print(g.hdop, 2);
    display.setCursor(72, 13);
    display.print("AGE ");
    if (g.differentialAge >= 0)
        display.print(g.differentialAge, 1);
    else
        display.print("--");
    display.setCursor(0, 27);
    display.print("RSSI ");
    display.print(stats.rssi);
    display.setCursor(72, 27);
    display.print("SNR ");
    display.print(stats.snr);
    display.setCursor(0, 41);
    display.print("RTCM ");
    display.print(stats.rtcmRate / 1000.0f, 2);
    display.print("kB/s");
    display.setCursor(0, 54);
    if (corrections.source == CorrectionInput::NTRIP) {
        display.print("NTRIP "); display.print(ntripState);
        display.display(); return;
    }
    display.print("P");
    display.print(radioControl.machine.active());
    display.print(radioControl.machine.pending() ? " TROCA" : radioControl.machine.rescue() ? " RESGATE"
                                                                                            : " LOSS ");
    display.print(lossPercent(), 1);
    display.print("%");
    display.display();
}
static void serviceGnss()
{
    unsigned budget = 256;
    while (budget-- && GNSSSerial.available())
    {
        char c = (char)GNSSSerial.read();
        gnss.feed(c);
        if (Serial.availableForWrite() > 0)
            Serial.write(c);
        static NmeaLine nmea{};
        static bool overflow = false;
        if (c == '$')
        {
            nmea.length = 0;
            overflow = false;
        }
        if (nmea.length < sizeof(nmea.data))
            nmea.data[nmea.length++] = (uint8_t)c;
        else
            overflow = true;
        if (c == '\n')
        {
            if (overflow)
                nmeaDrops++;
            else if (nmeaQueue && SerialBT.hasClient() && xQueueSend(nmeaQueue, &nmea, 0) != pdTRUE)
                nmeaDrops++;
            nmea.length = 0;
            overflow = false;
        }
        static uint32_t ggaCount = 0;
        if (gnss.status().ggaCount != ggaCount)
        {
            ggaCount = gnss.status().ggaCount;
            lastGgaAt = millis();
            const auto &g=gnss.status();
            directNtrip.gga(g.quality && isfinite(g.latitude) && isfinite(g.longitude) ? g.rawGga : "", lastGgaAt);
        }
        static uint32_t gst = 0, gsa = 0, rmc = 0;
        const auto &g = gnss.status();
        if (g.gstCount != gst)
        {
            gst = g.gstCount;
            lastGstAt = millis();
        }
        if (g.gsaCount != gsa)
        {
            gsa = g.gsaCount;
            lastGsaAt = millis();
        }
        if (g.rmcCount != rmc)
        {
            rmc = g.rmcCount;
            lastRmcAt = millis();
        }
    }
}
static bool startAccessPoint();
static void serviceUsb()
{
    while (Serial.available())
    {
        char c = Serial.read();
        if (c == '\r' || c == '\n')
        {
            usbLine.trim();
            if (usbLine == "STATUS")
                showStatus();
            else if (usbLine == "STATS_RESET")
            {
                stats = Stats{};
                sequenceTracker.reset();
                corrections.select(corrections.source);
                lastRtcmAt=0; maxRtcmGap=0;
                Serial.println("OK STATS_RESET");
            }
            else if (usbLine == "NET_INFO")
                Serial.printf("Wi-Fi: RTK-ROVER\nSenha: %s\nhttp://192.168.4.1\n", apPassword.c_str());
            else if (usbLine.startsWith("WIFI_PASS="))
            {
                String password = usbLine.substring(10);
                if (password.length() < 8 || password.length() > 63)
                    Serial.println("ERR WIFI_PASS_LENGTH_8_TO_63");
                else
                {
                    Preferences p;
                    p.begin("rover-web", false);
                    p.putString("password", password);
                    p.putBool("password_custom", true);
                    p.end();
                    apPassword = password;
                    Serial.println(startAccessPoint() ? "OK WIFI_PASS" : "ERR WIFI_AP");
                }
            }
            else if (usbLine == "PROFILE_LIST")
            {
                for (uint8_t i = 0; i < RadioControl::PROFILE_COUNT; i++)
                    Serial.printf("%u %s\n", i, RadioControl::profiles[i].name);
            }
            else if (usbLine.startsWith("PROFILE="))
            {
                int id = RadioControl::profileId(usbLine.substring(8).c_str());
                Serial.println(corrections.source==CorrectionInput::LORA && id >= 0 && radioControl.request(id) ? "OK REQUESTED" : "ERR PROFILE/BUSY/SOURCE");
            }
            else if (!usbLine.isEmpty())
                Serial.println("Comandos: STATUS, STATS_RESET, NET_INFO, WIFI_PASS=, PROFILE_LIST, PROFILE=nome");
            usbLine = "";
        }
        else if (usbLine.length() < 100)
            usbLine += c;
        else
            usbLine = "";
    }
}

static bool authorized()
{
    return true;
}
static bool startAccessPoint()
{
    WiFi.softAPdisconnect(true);
    delay(100);
    return WiFi.softAP("RTK-ROVER", apPassword.c_str());
}
static bool baseFresh() { return radioControl.machine.heardBase() && !radioControl.machine.rescue() && millis() - radioControl.machine.lastHeard() < 4000; }
#include "TelemetryJson.h"
#include "RoverNtripWeb.h"
static void setupWeb()
{
    Preferences p;
    p.begin("rover-web", false);
    bool custom = p.getBool("password_custom", false);
    apPassword = custom ? p.getString("password", "12345678") : "12345678";
    if (apPassword.length()<8 || apPassword.length()>63) { apPassword="12345678"; custom=false; p.putBool("password_custom",false); }
    if (!custom)
        p.putString("password", apPassword);
    p.end();
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    if (!startAccessPoint())
        Serial.println("ERR WIFI_AP");
    setupNtrip();
    web.on("/", HTTP_GET, []
           { web.sendHeader("Cache-Control","no-store"); web.send_P(200, "text/html; charset=utf-8", FIELD_PAGE); });
    web.on("/report.js", HTTP_GET, []
           {
        extern const uint8_t scriptStart[] asm("_binary_src_FieldReport_js_start");
        web.sendHeader("Cache-Control","no-store");web.send_P(200,"application/javascript; charset=utf-8",(const char*)scriptStart); });
    web.on("/status", HTTP_GET, []
           {
        const auto&g=gnss.status();const auto&m=radioControl.machine;
        String s="{\"source\":"+jsonText(corrections.name())+",\"token\":\"\",\"profiles\":[";
        for(uint8_t i=0;i<RadioControl::PROFILE_COUNT;i++){if(i)s+=",";s+="\""+String(RadioControl::profiles[i].name)+"\"";}
        s+="],\"active\":"+String(m.active())+",\"radio\":"+String(m.profile());
        s+=",\"ready\":"+String(radioControl.ready()?"true":"false")+",\"paired\":true";
        s+=",\"baseFresh\":"+String(baseFresh()?"true":"false")+",\"pending\":"+String(m.pending()?"true":"false");
        s+=",\"state\":\""+String(m.pending()?"Negociando":m.rescue()?"Procurando base no resgate":m.result()==2?"Confirmada por LoRa":m.result()==3?"Prazo esgotado; confira o perfil da base":"Pronto")+"\"";
        s+=",\"fix\":\""+String(lastGgaAt&&millis()-lastGgaAt<3000?GnssMonitor::fixText(g.quality):"GNSS sem dados recentes")+"\"";
        s+=",\"rate\":"+String(stats.rtcmRate)+",\"rtcmAge\":"+String(lastRtcmAt?(long)(millis()-lastRtcmAt):-1L);
        s+=",\"rssi\":"+String(stats.rssi)+",\"snr\":"+String(stats.snr)+",\"lost\":"+String(stats.lost);
        s+=",\"diffAge\":"+String(isfinite(g.differentialAge)?g.differentialAge:-1.0f,1);
        appendTelemetry(s);s+="}";
        web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",s); });
    web.on("/profile", HTTP_POST, []
           {if(!authorized())return;
        if(corrections.source!=CorrectionInput::LORA){web.send(409,"text/plain","Selecione LoRa para trocar o perfil do enlace.");return;}
        String value=web.arg("id");if(value.length()!=1||value[0]<'0'||value[0]>='0'+RadioControl::PROFILE_COUNT){web.send(400,"text/plain","Perfil invalido.");return;}
        bool ok=radioControl.request(value[0]-'0');web.send(ok?202:409,"text/plain",ok?"Pedido enviado para negociacao; aguarde a confirmacao RF.":"Operacao ocupada ou radio indisponivel."); });
    web.on("/test", HTTP_POST, []
           { web.send(410, "text/plain", "Atualize a pagina: medicao e historico agora ficam no celular."); });
    web.on("/results.csv", HTTP_GET, []
           { web.send(410, "text/plain", "Exporte o CSV pelo botao da pagina; os registros ficam no celular."); });
    web.begin();
    Serial.println("Pagina de campo pronta; Wi-Fi protegido.");
}
static void updateRate()
{
    static uint32_t last = 0;
    static uint64_t bytes = 0;
    if (millis() - last >= 1000)
    {
        uint32_t dt = millis() - last;
        stats.rtcmRate = (stats.rtcmBytes >= bytes ? stats.rtcmBytes - bytes : stats.rtcmBytes) * 1000 / dt;
        bytes = stats.rtcmBytes;
        last = millis();
    }
}

void setup()
{
    Serial.begin(115200);
    roverBoot = esp_random();
    LoRaSerial.setRxBufferSize(2048);
    GNSSSerial.setRxBufferSize(2048);
    GNSSSerial.setTxBufferSize(2048);
    LoRaSerial.begin(UART_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    GNSSSerial.begin(UART_BAUD, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
    SerialBT.begin("RTK-ROVER");
    nmeaQueue = xQueueCreate(16, sizeof(NmeaLine));
    if (nmeaQueue)
        xTaskCreate(bluetoothWriter, "nmea-bt", 4096, nullptr, 1, nullptr);
    Wire.begin(OLED_SDA, OLED_SCL);
    displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    if (displayReady)
    {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.print("RTK-ROVER iniciando");
        display.display();
    }
    corrections.parser.setCallback(onRtcm, nullptr);
    lr30.setHandler(onLr30, nullptr);
    radioControl.begin(false, 0);
    setupWeb();
    Serial.println("RTK-ROVER pronto; Bluetooth SPP somente NMEA.");
}
void loop()
{
    lr30.poll();
    radioControl.service();
    static uint8_t lastProfile = 255;
    if (lastProfile != radioControl.machine.profile())
    {
        lastProfile = radioControl.machine.profile();
        corrections.reset(CorrectionInput::LORA);
        sequenceTracker.reset();
    }
    serviceUsb();
    serviceGnss();
    serviceDirectNtrip();
    lr30.poll();
    radioControl.service();
    web.handleClient();
    if(applyCorrectionsPending) { applyCorrectionsPending=false; applyCorrectionSource(); }
    updateRate();
    updateDisplay();
}
