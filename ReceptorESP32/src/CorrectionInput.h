#pragma once
#include <Rtcm3Parser.h>

// One parser and one selected source: a partial message cannot cross a switch.
class CorrectionInput {
public:
    enum Source { LORA, NTRIP };
    Source source = LORA;
    uint32_t session = 0;
    Rtcm3Parser parser;
    void select(Source next) { source = next; ++session; parser.reset(); }
    void reset(Source from) { if (from == source) parser.reset(); }
    void feed(Source from, const uint8_t *bytes, size_t length) {
        if (from == source) parser.feed(bytes, length);
    }
    const char *name() const { return source == NTRIP ? "NTRIP" : "LORA"; }
};
