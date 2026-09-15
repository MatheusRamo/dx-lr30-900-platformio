#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <NtripResponse.h>
#include <CorrectionInput.h>
#include <Crc24Q.h>
#define CHECK_N(x) do { if(!(x)) { fprintf(stderr,"NTRIP FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)

static std::vector<uint8_t> frame(uint16_t type) {
    std::vector<uint8_t> out(26,0); out[0]=0xd3; out[2]=20; out[3]=type>>4; out[4]=(type&15)<<4;
    // Bytes which are significant in HTTP must survive unchanged.
    out[5]='\r'; out[6]='\n'; out[7]=0; out[8]=0xff;
    uint32_t crc=crc24q(out.data(),23); out[23]=crc>>16; out[24]=crc>>8; out[25]=crc;
    return out;
}
static std::vector<uint8_t> decode(NtripResponse &p,const std::string &wire) {
    std::vector<uint8_t> body;
    for(unsigned char b:wire) { auto r=p.feed(b); if(r==NtripResponse::BYTE)body.push_back(b); }
    return body;
}
static int frames=0, errors=0;
static void capture(void*,const uint8_t*,size_t,uint16_t,bool valid) { if(valid)++frames; else ++errors; }
void runNtripTests() {
    const auto rtcm=frame(1077); const std::string binary((const char*)rtcm.data(),rtcm.size());
    for(const auto &header:{"ICY 200 OK\r\n", "HTTP/1.0 200 OK\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Type: gnss/data\r\nServer: caster\r\n\r\n"}) {
        NtripResponse p;
        const std::string expected=binary+binary;
        CHECK_N(decode(p,std::string(header)+expected)==std::vector<uint8_t>(expected.begin(),expected.end()));
        CHECK_N(p.streaming() && p.code==200);
    }
    NtripResponse p;
    auto body=decode(p,"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;ext=value\r\n"+binary.substr(0,5)+"\r\n15\r\n"+binary.substr(5)+"\r\n0\r\n\r\n");
    CHECK_N(body==rtcm); CHECK_N(p.feed(0)==NtripResponse::END);
    for(const auto &response:{"HTTP/1.1 401 Unauthorized\r\n", "HTTP/1.0 403 Forbidden\r\n", "HTTP/1.1 404 Missing\r\n", "HTTP/1.1 2000 Nope\r\n", "SOURCETABLE 200 OK\r\n", "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n"}) {
        p.reset(); CHECK_N(decode(p,response).empty()); CHECK_N(p.feed(0)==NtripResponse::ERROR);
    }
    for(const auto &chunks:{"z\r\n", "100000000\r\n", ";x\r\n", "1\r\nx!", "1\r\nx\r!"}) {
        p.reset(); decode(p,std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")+chunks);
        CHECK_N(p.feed(0)==NtripResponse::ERROR);
    }
    p.reset(); decode(p,std::string(513,'x')); CHECK_N(p.feed(0)==NtripResponse::ERROR);
    p.reset(); decode(p,"HTTP/1.1 200 OK\r\n"); for(int i=0;i<5000;++i)decode(p,"x\n"); CHECK_N(p.feed(0)==NtripResponse::ERROR);
    p.reset(); CHECK_N(decode(p,"ICY 200 OK\r\n"+binary)==rtcm); // recovery after failure

    CorrectionInput input; input.parser.setCallback(capture,nullptr);
    input.feed(CorrectionInput::LORA,rtcm.data(),7);
    input.select(CorrectionInput::NTRIP);
    CHECK_N(input.session==1);
    input.feed(CorrectionInput::LORA,rtcm.data()+7,rtcm.size()-7); CHECK_N(frames==0);
    input.feed(CorrectionInput::NTRIP,rtcm.data(),8);
    input.reset(CorrectionInput::LORA); // modem profile/rescue must not reset direct NTRIP
    input.feed(CorrectionInput::LORA,rtcm.data(),rtcm.size()); CHECK_N(frames==0);
    input.feed(CorrectionInput::NTRIP,rtcm.data()+8,rtcm.size()-8); CHECK_N(frames==1);
    input.feed(CorrectionInput::NTRIP,rtcm.data(),9);
    input.select(CorrectionInput::LORA);
    input.feed(CorrectionInput::NTRIP,rtcm.data()+9,rtcm.size()-9); CHECK_N(frames==1);
    input.feed(CorrectionInput::LORA,rtcm.data(),rtcm.size()); CHECK_N(frames==2);
    auto bad=rtcm; bad[8]^=1; input.feed(CorrectionInput::LORA,bad.data(),bad.size()); CHECK_N(frames==2 && errors==1);
    puts("NTRIP tests passed: ICY/HTTP, binary body, chunking, rejected responses, source isolation and CRC");
}
