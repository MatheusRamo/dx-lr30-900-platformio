#pragma once
#include <stddef.h>
#include <stdint.h>
#include <math.h>

struct GnssStatus {
    uint32_t ggaCount = 0, gstCount=0, gsaCount=0, rmcCount=0;
    double latitude = NAN, longitude = NAN, altitude = NAN, geoidSeparation=NAN;
    uint8_t quality = 0, satellites = 0;
    float hdop=NAN, pdop=NAN, vdop=NAN, differentialAge=NAN;
    float sigmaLatitude=NAN,sigmaLongitude=NAN,sigmaAltitude=NAN,gstRms=NAN;
    float sigmaMajor=NAN,sigmaMinor=NAN,ellipseOrientation=NAN,speedKnots=NAN,course=NAN;
    char utc[16]={},date[7]={},gstUtc[16]={},stationId[12]={};
    char rawGga[192]={},rawGst[192]={},rawGsa[192]={},rawRmc[192]={};
    bool hasGga = false, hasRmc = false, rmcValid = false;
};

class GnssMonitor {
public:
    void feed(char byte);
    const GnssStatus &status() const { return status_; }
    static const char *fixText(uint8_t quality);
private:
    void parseLine();
    static double coordinate(const char *value, char hemisphere);
    char line_[192] = {}; size_t used_ = 0; GnssStatus status_;
};
