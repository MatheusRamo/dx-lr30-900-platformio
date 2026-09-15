#include "GnssMonitor.h"
#include <stdlib.h>
#include <string.h>

static int hexDigit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}
static bool checksumOk(const char*line){
    if(line[0]!='$')return false;const char*star=strchr(line,'*');if(!star||strlen(star)!=3)return false;
    int a=hexDigit(star[1]),b=hexDigit(star[2]);if(a<0||b<0)return false;
    uint8_t sum=0;for(const char*p=line+1;p<star;p++)sum^=(uint8_t)*p;return sum==((a<<4)|b);
}
static double number(const char*s){if(!s||!*s)return NAN;char*end;double v=strtod(s,&end);return *end||!isfinite(v)?NAN:v;}
static double nonnegative(const char*s){double v=number(s);return v>=0?v:NAN;}
template<size_t N> static void copyText(char(&out)[N],const char*s){strncpy(out,s,N-1);out[N-1]=0;}
void GnssMonitor::feed(char b){
    if(b=='\r')return;if(b=='\n'){if(used_){line_[used_]=0;parseLine();used_=0;}return;}
    if(b=='$')used_=0;if(used_<sizeof(line_)-1)line_[used_++]=b;else used_=0;
}
double GnssMonitor::coordinate(const char*v,char h){
    double raw=number(v);if(!isfinite(raw)||raw<0||(h!='N'&&h!='S'&&h!='E'&&h!='W'))return NAN;
    double deg=floor(raw/100),minutes=raw-deg*100,result=deg+minutes/60;
    if(minutes>=60||result>((h=='N'||h=='S')?90:180))return NAN;
    return(h=='S'||h=='W')?-result:result;
}
const char*GnssMonitor::fixText(uint8_t q){switch(q){case 1:return "GNSS";case 2:return "DGPS";case 4:return "RTK FIXED";case 5:return "RTK FLOAT";case 6:return "ESTIMATED";default:return "NO FIX";}}
void GnssMonitor::parseLine(){
    if(!checksumOk(line_))return;char text[192];copyText(text,line_);*strchr(text,'*')=0;
    char*fields[24]={};size_t count=0;char*p=text;
    while(count<24){fields[count++]=p;char*comma=strchr(p,',');if(!comma)break;*comma=0;p=comma+1;}
    if(strlen(fields[0])!=6)return;const char*kind=fields[0]+3;
    if(!strcmp(kind,"GGA")&&count>=15){
        status_.ggaCount++;copyText(status_.rawGga,line_);copyText(status_.utc,fields[1]);copyText(status_.stationId,fields[14]);
        double quality=nonnegative(fields[6]);status_.quality=isfinite(quality)&&quality<=8&&floor(quality)==quality?(uint8_t)quality:0;
        status_.latitude=status_.quality&&(fields[3][0]=='N'||fields[3][0]=='S')?coordinate(fields[2],fields[3][0]):NAN;
        status_.longitude=status_.quality&&(fields[5][0]=='E'||fields[5][0]=='W')?coordinate(fields[4],fields[5][0]):NAN;
        double sats=nonnegative(fields[7]);status_.satellites=isfinite(sats)&&sats<=255?(uint8_t)sats:0;
        status_.hdop=nonnegative(fields[8]);status_.altitude=status_.quality&&strcmp(fields[10],"M")==0?number(fields[9]):NAN;
        status_.geoidSeparation=strcmp(fields[12],"M")==0?number(fields[11]):NAN;
        status_.differentialAge=nonnegative(fields[13]);status_.hasGga=true;
    }else if(!strcmp(kind,"GST")&&count>=9){
        status_.gstCount++;copyText(status_.rawGst,line_);copyText(status_.gstUtc,fields[1]);
        status_.gstRms=nonnegative(fields[2]);status_.sigmaMajor=nonnegative(fields[3]);status_.sigmaMinor=nonnegative(fields[4]);
        status_.ellipseOrientation=nonnegative(fields[5]);status_.sigmaLatitude=nonnegative(fields[6]);
        status_.sigmaLongitude=nonnegative(fields[7]);status_.sigmaAltitude=nonnegative(fields[8]);
    }else if(!strcmp(kind,"GSA")&&count>=18){
        status_.gsaCount++;copyText(status_.rawGsa,line_);status_.pdop=nonnegative(fields[15]);status_.vdop=nonnegative(fields[17]);
    }else if(!strcmp(kind,"RMC")&&count>=10){
        status_.rmcCount++;copyText(status_.rawRmc,line_);status_.rmcValid=fields[2][0]=='A';status_.hasRmc=true;
        copyText(status_.date,fields[9]);status_.speedKnots=status_.rmcValid?nonnegative(fields[7]):NAN;
        status_.course=status_.rmcValid?nonnegative(fields[8]):NAN;
    }
}
