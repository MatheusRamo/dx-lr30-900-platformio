#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

// Incremental decoder: strips HTTP headers and chunk framing before RTCM CRC.
class NtripResponse {
public:
    enum Result { WAIT, BYTE, ERROR, END };
    void reset() { state = STATUS; used = headers = remaining = 0; chunked = false; code = 0; reason = ""; }
    int code = 0;
    const char *reason = "";
    bool streaming() const { return state == BODY || state == SIZE || state == CHUNK || state == CR || state == LF; }
    Result feed(uint8_t b) {
        if (state == FAILED) return ERROR;
        if (state == DONE) return END;
        if (state == BODY) return BYTE;
        if (state == CHUNK) { if (--remaining == 0) state = CR; return BYTE; }
        if (state == CR) { if (b != '\r') return fail("CHUNK_INVALID"); state = LF; return WAIT; }
        if (state == LF) { if (b != '\n') return fail("CHUNK_INVALID"); state = SIZE; return WAIT; }
        if ((state == STATUS || state == HEADERS) && ++headers > 8192) return fail("HEADERS_TOO_LARGE");
        if (b != '\n') {
            if (b == 0 || used >= sizeof(line)-1) return fail("LINE_INVALID");
            line[used++] = (char)b; return WAIT;
        }
        if (used && line[used-1] == '\r') --used;
        line[used] = 0;
        if (state == STATUS) {
            const char *p = nullptr;
            bool icy = strncmp(line, "ICY ", 4) == 0;
            if (icy) p = line + 4;
            else if (strncmp(line, "HTTP/1.0 ", 9) == 0 || strncmp(line, "HTTP/1.1 ", 9) == 0) p = line + 9;
            if (!p || strlen(p) < 3 || !isdigit(p[0]) || !isdigit(p[1]) || !isdigit(p[2]) || (p[3] && p[3] != ' ')) return fail("STATUS_INVALID_OR_SOURCETABLE");
            code = (p[0]-'0')*100+(p[1]-'0')*10+p[2]-'0';
            if (code != 200) return fail(code == 401 || code == 403 ? "AUTH_REJECTED" : "CASTER_REJECTED");
            state = icy ? BODY : HEADERS;
        } else if (state == HEADERS) {
            if (!used) state = chunked ? SIZE : BODY;
            else {
                for (size_t i=0;i<used;++i) line[i] = (char)tolower((unsigned char)line[i]);
                if (strncmp(line,"transfer-encoding:",18)==0) {
                    const char *p=line+18; while (*p==' ' || *p=='\t') ++p;
                    if (strcmp(p,"chunked")!=0) return fail("ENCODING_UNSUPPORTED");
                    chunked=true;
                }
                if (strncmp(line,"content-type:",13)==0 && (strstr(line,"text/") || strstr(line,"sourcetable"))) return fail("NOT_RTCM_STREAM");
                if (strncmp(line,"content-encoding:",17)==0 && !strstr(line,"identity")) return fail("ENCODING_UNSUPPORTED");
            }
        } else if (state == SIZE) {
            remaining = 0; size_t i=0;
            for (;i<used && line[i]!=';';++i) {
                int c=tolower((unsigned char)line[i]);
                int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
                if (n<0 || remaining>0x0fffffffU) return fail("CHUNK_INVALID");
                remaining=remaining*16+(unsigned)n;
            }
            if (!i) return fail("CHUNK_INVALID");
            if (!remaining) { state=DONE; used=0; return END; }
            state=CHUNK;
        }
        used=0; return WAIT;
    }
private:
    enum State { STATUS, HEADERS, BODY, SIZE, CHUNK, CR, LF, FAILED, DONE } state = STATUS;
    char line[512]{};
    size_t used=0, headers=0;
    uint32_t remaining=0;
    bool chunked=false;
    Result fail(const char *message) { reason=message; state=FAILED; return ERROR; }
};
