#include <GnssMonitor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define GNSS_CHECK(x) do{if(!(x)){fprintf(stderr,"GNSS FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void sentence(GnssMonitor&m,const char*body){
    uint8_t sum=0;for(const char*p=body;*p;p++)sum^=*p;
    char line[240];snprintf(line,sizeof(line),"$%s*%02X\r\n",body,sum);for(const char*p=line;*p;p++)m.feed(*p);
}
void runGnssTests(){
    GnssMonitor m;GNSS_CHECK(isnan(m.status().latitude)&&isnan(m.status().sigmaLatitude));
    sentence(m,"GNGGA,123456.00,2300.000000,S,04600.000000,W,4,18,0.6,750.123,M,-3.5,M,1.2,1234");
    const auto&g=m.status();GNSS_CHECK(g.ggaCount==1&&g.quality==4&&g.latitude==-23&&g.longitude==-46);
    GNSS_CHECK(fabs(g.altitude-750.123)<1e-6&&fabs(g.geoidSeparation+3.5)<1e-6);GNSS_CHECK(!strcmp(g.stationId,"1234"));
    sentence(m,"GNGST,123456.00,0.010,0.012,0.008,30.0,0.009,0.010,0.020");
    GNSS_CHECK(g.gstCount==1&&fabs(g.sigmaLatitude-0.009)<1e-6&&fabs(g.sigmaAltitude-0.02)<1e-6);
    sentence(m,"GNGSA,A,3,01,02,03,04,05,06,07,08,09,10,11,12,1.2,0.6,0.9");
    GNSS_CHECK(g.gsaCount==1&&fabs(g.pdop-1.2)<1e-5&&fabs(g.vdop-0.9)<1e-5);
    sentence(m,"GNRMC,123456.00,A,2300.0,S,04600.0,W,0.1,80.0,140926,,,A");
    GNSS_CHECK(g.rmcCount==1&&g.rmcValid&&!strcmp(g.date,"140926"));
    sentence(m,"GNGGA,123457.00,,,,,0,00,,,,,,, ");GNSS_CHECK(g.quality==0&&isnan(g.latitude)&&isnan(g.altitude));
    sentence(m,"GNGST,123457.00,,,,,,,");GNSS_CHECK(isnan(g.sigmaLatitude)&&isnan(g.gstRms));
    sentence(m,"GNGGA,123458.00,2360.000,N,04600.0,W,5,10,1,10,M,0,M,,");GNSS_CHECK(g.quality==5&&isnan(g.latitude));
    sentence(m,"GNGGA,123459.00,2300.0,E,04600.0,N,4,10,1,10,M,0,M,,");GNSS_CHECK(isnan(g.latitude)&&isnan(g.longitude));
    uint32_t before=g.ggaCount;const char*bad="$GNGGA,123456.00,2300.0,S,04600.0,W,4,18,0.6,750,M,0,M,1,1234*FF\n";
    while(*bad)m.feed(*bad++);GNSS_CHECK(g.ggaCount==before);
    puts("GNSS telemetry tests passed");
}
