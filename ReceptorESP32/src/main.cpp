#include <Arduino.h>
#include <esp_heap_caps.h>
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
bool displayReady = false, bluetoothReady = false, accessPointReady = false;
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
String bluetoothLine;
bool bluetoothWasConnected = false;
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
            else if (nmeaQueue && SerialBT.hasClient())
            {
                // SW Maps needs a steady position stream. In NTRIP mode keep
                // SPP light by sending only the 1 Hz GGA/RMC pair; LoRa mode
                // preserves the complete NMEA stream for diagnostics.
                const bool mappingSentence = nmea.length >= 6 &&
                    ((nmea.data[3] == 'G' && nmea.data[4] == 'G' && nmea.data[5] == 'A') ||
                     (nmea.data[3] == 'R' && nmea.data[4] == 'M' && nmea.data[5] == 'C'));
                if (corrections.source == CorrectionInput::LORA || mappingSentence)
                    if (xQueueSend(nmeaQueue, &nmea, 0) != pdTRUE)
                        nmeaDrops++;
            }
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
                Serial.printf("AP: %s, pronto=%d, IP=%s, canal=%u, clientes=%u, modo=%u\nSenha: %s\nHTTP: http://192.168.4.1/ e http://192.168.4.1/ping\nBluetooth: pronto=%d, nome=RTK-ROVER, PIN=1234, conectado=%d\nHeap livre=%u, maior bloco=%u\n",
                              WiFi.softAPSSID().c_str(), accessPointReady, WiFi.softAPIP().toString().c_str(), WiFi.channel(),
                              WiFi.softAPgetStationNum(), (unsigned)WiFi.getMode(), apPassword.c_str(), bluetoothReady, SerialBT.hasClient(),
                              ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
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
    // The AP is kept alive while using LoRa and NTRIP. Repeatedly disabling it
    // here made phones lose DHCP/HTTP during Wi-Fi mode transitions.
    if (!(WiFi.getMode() & WIFI_MODE_AP))
        WiFi.mode((wifi_mode_t)(WiFi.getMode() | WIFI_MODE_AP));
    accessPointReady = WiFi.softAP("RTK-ROVER", apPassword.c_str(), 1, 0, 4);
    return accessPointReady;
}
static bool baseFresh() { return radioControl.machine.heardBase() && !radioControl.machine.rescue() && millis() - radioControl.machine.lastHeard() < 4000; }
#include "TelemetryJson.h"
#include "RoverNtripWeb.h"
static void bluetoothReply(const String &message)
{
    if (bluetoothReady && SerialBT.hasClient())
        SerialBT.println(message);
}
static void bluetoothStatus()
{
    String mode = corrections.source == CorrectionInput::NTRIP ? "NTRIP" : "LORA";
    String wifi = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "OFF";
    String fix = lastGgaAt && millis() - lastGgaAt < 3000 ? GnssMonitor::fixText(gnss.status().quality) : "STALE";
    bluetoothReply("STATUS MODE=" + mode + " WIFI=" + wifi + " NTRIP=" + ntripState +
                   " RTCM_BPS=" + String(stats.rtcmRate) + " FIX=" + fix +
                   " AP_CLIENTS=" + String(WiFi.softAPgetStationNum()) +
                   " HEAP=" + String(ESP.getFreeHeap()));
}
static bool bluetoothSetField(const String &line, const char *prefix, char *destination, size_t capacity)
{
    if (!line.startsWith(prefix))
        return false;
    String value = line.substring(strlen(prefix));
    if (value.length() >= capacity || strlen(value.c_str()) != value.length())
    {
        bluetoothReply(String("ERR ") + prefix + "_LENGTH");
        return true;
    }
    value.toCharArray(destination, capacity);
    bluetoothReply(String("OK ") + prefix);
    return true;
}
static void serviceBluetooth()
{
    const bool connected = bluetoothReady && SerialBT.hasClient();
    if (!connected)
    {
        bluetoothWasConnected = false;
        bluetoothLine = "";
        return;
    }
    if (!bluetoothWasConnected)
    {
        bluetoothWasConnected = true;
        bluetoothReply("RTK-ROVER BT CONFIG READY PIN=1234");
        bluetoothReply("Use HELP for commands; NMEA output remains enabled.");
    }
    while (SerialBT.available())
    {
        char c = (char)SerialBT.read();
        if (c != '\r' && c != '\n')
        {
            if (bluetoothLine.length() < 180)
                bluetoothLine += c;
            else
                bluetoothLine = "";
            continue;
        }
        bluetoothLine.trim();
        if (bluetoothLine.isEmpty())
            continue;
        const String line = bluetoothLine;
        bluetoothLine = "";
        if (line == "HELP")
        {
            bluetoothReply("COMMANDS: STATUS, MODE=1|2, NTRIP_SSID=, NTRIP_WIFI_PASS=, NTRIP_HOST=, NTRIP_PORT=, NTRIP_MOUNT=, NTRIP_USER=, NTRIP_PASS=, NTRIP_GGA=0|1, NTRIP_SAVE, PROFILE_LIST, PROFILE=, PING");
            bluetoothReply("MODE 1=LORA 2=NTRIP; use NTRIP_SAVE after editing fields");
        }
        else if (line == "PING")
            bluetoothReply("PONG");
        else if (line == "STATUS")
            bluetoothStatus();
        else if (line == "PROFILE_LIST")
        {
            for (uint8_t i = 0; i < RadioControl::PROFILE_COUNT; i++)
                bluetoothReply(String(i) + " " + RadioControl::profiles[i].name);
        }
        else if (line.startsWith("PROFILE="))
        {
            int id = RadioControl::profileId(line.substring(8).c_str());
            bluetoothReply(corrections.source == CorrectionInput::LORA && id >= 0 && radioControl.request(id)
                               ? "OK PROFILE_REQUESTED"
                               : "ERR PROFILE_BUSY_OR_NTRIP");
        }
        else if (line.startsWith("MODE="))
        {
            String value = line.substring(5);
            const bool ntrip = value == "2" || value == "NTRIP";
            const bool lora = value == "1" || value == "LORA";
            if (!lora && !ntrip)
                bluetoothReply("ERR MODE_USE_1_LORA_OR_2_NTRIP");
            else if (ntrip && !configValid(ntripConfig))
                bluetoothReply("ERR NTRIP_CONFIG_INCOMPLETE");
            else if (ntrip && !ntripWorkerReady && !(ntripWorkerReady = directNtrip.begin()))
                bluetoothReply("ERR NTRIP_MEMORY");
            else if (radioControl.machine.pending())
                bluetoothReply("ERR RADIO_PROFILE_NEGOTIATION_BUSY");
            else
            {
                CorrectionInput::Source previous = corrections.source;
                corrections.source = ntrip ? CorrectionInput::NTRIP : CorrectionInput::LORA;
                if (!saveNtripConfig())
                {
                    corrections.source = previous;
                    bluetoothReply("ERR NVS_SAVE");
                }
                else
                {
                    applyCorrectionsPending = true;
                    bluetoothReply(String("OK MODE=") + (ntrip ? "2 NTRIP" : "1 LORA"));
                }
            }
        }
        else if (bluetoothSetField(line, "NTRIP_SSID=", ntripConfig.ssid, sizeof(ntripConfig.ssid)))
            ;
        else if (bluetoothSetField(line, "NTRIP_WIFI_PASS=", ntripConfig.wifiPass, sizeof(ntripConfig.wifiPass)))
            ;
        else if (bluetoothSetField(line, "NTRIP_HOST=", ntripConfig.host, sizeof(ntripConfig.host)))
            ;
        else if (bluetoothSetField(line, "NTRIP_MOUNT=", ntripConfig.mount, sizeof(ntripConfig.mount)))
            ;
        else if (bluetoothSetField(line, "NTRIP_USER=", ntripConfig.user, sizeof(ntripConfig.user)))
            ;
        else if (bluetoothSetField(line, "NTRIP_PASS=", ntripConfig.password, sizeof(ntripConfig.password)))
            ;
        else if (line.startsWith("NTRIP_PORT="))
        {
            String value = line.substring(11);
            bool valid = !value.isEmpty();
            for (size_t i = 0; i < value.length(); i++)
                valid = valid && isdigit((unsigned char)value[i]);
            long port = value.toInt();
            if (!valid || port < 1 || port > 65535)
                bluetoothReply("ERR NTRIP_PORT");
            else
            {
                ntripConfig.port = (uint16_t)port;
                bluetoothReply("OK NTRIP_PORT");
            }
        }
        else if (line.startsWith("NTRIP_GGA="))
        {
            String value = line.substring(10);
            if (value != "0" && value != "1")
                bluetoothReply("ERR NTRIP_GGA_USE_0_OR_1");
            else
            {
                ntripConfig.sendGga = value == "1";
                bluetoothReply("OK NTRIP_GGA");
            }
        }
        else if (line == "NTRIP_SAVE" || line == "SAVE")
        {
            if (!configValid(ntripConfig))
                bluetoothReply("ERR NTRIP_CONFIG_INVALID");
            else if (!saveNtripConfig())
                bluetoothReply("ERR NVS_SAVE");
            else
            {
                if (corrections.source == CorrectionInput::NTRIP)
                    applyCorrectionsPending = true;
                bluetoothReply("OK NTRIP_SAVED");
            }
        }
        else if (line == "NTRIP_CLEAR_WIFI")
        {
            ntripConfig.wifiPass[0] = 0;
            bluetoothReply("OK NTRIP_WIFI_PASSWORD_CLEARED");
        }
        else if (line == "NTRIP_CLEAR_PASS")
        {
            ntripConfig.password[0] = 0;
            bluetoothReply("OK NTRIP_PASSWORD_CLEARED");
        }
        else
            bluetoothReply("ERR UNKNOWN_COMMAND; SEND HELP");
    }
}
static bool sendWebAsset(const char *contentType, PGM_P content, size_t length)
{
    web.setContentLength(length);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, contentType, "");
    WiFiClient client = web.client();
    client.setNoDelay(true);
    constexpr size_t CHUNK = 512;
    for (size_t offset = 0; offset < length; offset += CHUNK)
    {
        const size_t count = min(CHUNK, length - offset);
        if (!client.connected() || client.write_P(content + offset, count) != count)
        {
            Serial.printf("HTTP asset interrompido em %u/%u bytes\n", (unsigned)offset, (unsigned)length);
            client.stop();
            return false;
        }
        delay(2); // Let the Wi-Fi task ACK data before filling the socket buffer.
    }
    return true;
}
static bool sendWebText(const char *contentType, const String &content)
{
    web.setContentLength(content.length());
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, contentType, "");
    WiFiClient client = web.client();
    client.setNoDelay(true);
    constexpr size_t CHUNK = 512;
    for (size_t offset = 0; offset < content.length(); offset += CHUNK)
    {
        const size_t count = min(CHUNK, content.length() - offset);
        if (!client.connected() || client.write((const uint8_t *)content.c_str() + offset, count) != count)
        {
            Serial.printf("HTTP texto interrompido em %u/%u bytes\n", (unsigned)offset, (unsigned)content.length());
            client.stop();
            return false;
        }
        delay(2);
    }
    return true;
}
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
    WiFi.mode(WIFI_AP);
    if (!startAccessPoint())
        Serial.println("ERR WIFI_AP");
    setupNtrip();
    web.on("/", HTTP_GET, []
           { sendWebAsset("text/html; charset=utf-8", FIELD_PAGE, strlen_P(FIELD_PAGE)); });
    web.on("/generate_204", HTTP_GET, []
           { web.sendHeader("Location", "http://192.168.4.1/", true); web.send(302, "text/plain", ""); });
    web.on("/hotspot-detect.html", HTTP_GET, []
           { web.send(200, "text/html; charset=utf-8", "<!doctype html><meta charset=utf-8><title>RTK-ROVER</title><a href='http://192.168.4.1/'>Abrir configuracao RTK-ROVER</a>"); });
    web.on("/connecttest.txt", HTTP_GET, []
           { web.sendHeader("Location", "http://192.168.4.1/", true); web.send(302, "text/plain", ""); });
    web.on("/ping", HTTP_GET, []
           { web.send(200, "text/plain", "RTK-ROVER OK"); });
    web.on("/report.js", HTTP_GET, []
           {
        extern const uint8_t scriptStart[] asm("_binary_src_FieldReport_js_start");
        extern const uint8_t scriptEnd[] asm("_binary_src_FieldReport_js_end");
        sendWebAsset("application/javascript; charset=utf-8",(const char*)scriptStart,(size_t)(scriptEnd-scriptStart-1)); });
    web.on("/status", HTTP_GET, []
           {
        const auto&m=radioControl.machine;
        String s;
        if(!s.reserve(4096)){web.send(503,"text/plain","Memoria temporaria insuficiente; tente novamente.");return;}
        s="{\"token\":\"\",\"profiles\":[";
        for(uint8_t i=0;i<RadioControl::PROFILE_COUNT;i++){if(i)s+=",";s+="\""+String(RadioControl::profiles[i].name)+"\"";}
        s+="],\"active\":"+String(m.active())+",\"paired\":true";
        s+=",\"baseFresh\":"+String(baseFresh()?"true":"false")+",\"pending\":"+String(m.pending()?"true":"false");
        s+=",\"state\":\""+String(m.pending()?"Negociando":m.rescue()?"Procurando base no resgate":m.result()==2?"Confirmada por LoRa":m.result()==3?"Prazo esgotado; confira o perfil da base":"Pronto")+"\"";
        appendTelemetry(s,true);s+="}";
        static bool sizeLogged=false;
        if(!sizeLogged){Serial.printf("HTTP status compacto: %u bytes, heap=%u, maior bloco=%u\n",(unsigned)s.length(),ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));sizeLogged=true;}
        sendWebText("application/json",s); });
    web.on("/profile", HTTP_POST, []
           {if(!authorized())return;
        if(corrections.source!=CorrectionInput::LORA){web.send(409,"text/plain","Selecione LoRa para trocar o perfil do enlace.");return;}
        String value=web.arg("id");if(value.length()!=1||value[0]<'0'||value[0]>='0'+RadioControl::PROFILE_COUNT){web.send(400,"text/plain","Perfil invalido.");return;}
        bool ok=radioControl.request(value[0]-'0');web.send(ok?202:409,"text/plain",ok?"Pedido enviado para negociacao; aguarde a confirmacao RF.":"Operacao ocupada ou radio indisponivel."); });
    web.on("/test", HTTP_POST, []
           { web.send(410, "text/plain", "Atualize a pagina: medicao e historico agora ficam no celular."); });
    web.on("/results.csv", HTTP_GET, []
           { web.send(410, "text/plain", "Exporte o CSV pelo botao da pagina; os registros ficam no celular."); });
    web.onNotFound([]
                   { web.sendHeader("Location", "http://192.168.4.1/", true); web.send(302, "text/plain", "Abra http://192.168.4.1/"); });
    web.begin();
    Serial.printf("Pagina pronta: AP=%d SSID=%s IP=%s canal=%u clientes=%u\n", accessPointReady,
                  WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str(), WiFi.channel(), WiFi.softAPgetStationNum());
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
    bluetoothReady = SerialBT.begin("RTK-ROVER");
    if (bluetoothReady)
        SerialBT.setPin("1234");
    Serial.printf("Bluetooth: %s, nome RTK-ROVER, PIN 1234\n", bluetoothReady ? "OK" : "ERRO");
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
    Serial.println("RTK-ROVER pronto; Bluetooth SPP para NMEA e configuracao.");
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
    serviceBluetooth();
    serviceDirectNtrip();
    lr30.poll();
    radioControl.service();
    web.handleClient();
    if(applyCorrectionsPending) { applyCorrectionsPending=false; applyCorrectionSource(); }
    updateRate();
    updateDisplay();
}
