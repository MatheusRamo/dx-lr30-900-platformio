#pragma once
#include <stdint.h>
#include <string.h>

namespace RadioControl {
struct Profile { const char *name; uint8_t sf; uint32_t bandwidth; uint8_t cr; uint16_t preamble; };
constexpr uint8_t PROFILE_VERSION = 1;
constexpr Profile profiles[] = {
    {"RTK_FAST",5,500000,5,8}, {"RTK_FAST_P12",5,500000,5,12},
    {"RTK_BALANCED",6,500000,5,12}, {"RTK_RANGE",7,500000,5,12},
    {"RTK_RANGE_250",7,250000,5,12}, {"RTK_RANGE_SF8",8,250000,5,12},
    {"RTK_RANGE_SF9",9,250000,5,12}, {"RTK_RANGE_CR46",8,250000,6,12}
};
constexpr uint8_t PROFILE_COUNT = sizeof(profiles)/sizeof(profiles[0]);
constexpr uint8_t RESCUE_PROFILE = 5;
inline int profileId(const char *name) {
    for(uint8_t i=0;i<PROFILE_COUNT;i++) if(!strcmp(name,profiles[i].name)) return i;
    return -1;
}
inline uint32_t airtimeMs(uint8_t id, uint8_t bytes) {
    const auto &p=profiles[id < PROFILE_COUNT ? id : 0];
    int n=8*bytes+16-4*p.sf+20+(p.sf>6?8:0);
    int symbols=((n+4*p.sf-1)/(4*p.sf))*p.cr+p.preamble+12+(p.sf<=6?2:0);
    uint32_t numerator=(4*symbols+1)*(1UL<<(p.sf-2))*1000UL;
    return (numerator+p.bandwidth-1)/p.bandwidth;
}
inline bool due(uint32_t now,uint32_t deadline) { return (int32_t)(now-deadline)>=0; }
}
