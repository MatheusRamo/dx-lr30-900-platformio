#pragma once
#include <stddef.h>
#include <stdint.h>

namespace RtcmRadio {
constexpr uint8_t MAGIC0 = 'R';
constexpr uint8_t MAGIC1 = 'T';
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_SIZE = 9;
constexpr size_t STREAM_CHUNK_MAX = 240;
constexpr size_t PACKET_MAX = HEADER_SIZE + STREAM_CHUNK_MAX;

struct Header { uint16_t session; uint16_t sequence; uint8_t length; uint8_t flags; };
bool encode(uint8_t *output, size_t capacity, const Header &header,
            const uint8_t *stream, size_t length, size_t &written);
bool decode(const uint8_t *packet, size_t packetLength, Header &header,
            const uint8_t *&stream);

enum class SequenceResult { FIRST, ACCEPT, LOSS, DUPLICATE, SESSION_CHANGED };
class SequenceTracker {
public:
    SequenceResult observe(uint16_t session, uint16_t sequence, uint16_t &lost);
    void reset();
private:
    bool initialized_ = false; uint16_t session_ = 0; uint16_t last_ = 0;
};
}
