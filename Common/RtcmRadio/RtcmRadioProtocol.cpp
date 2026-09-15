#include "RtcmRadioProtocol.h"
#include <string.h>

namespace RtcmRadio {
bool encode(uint8_t *out, size_t capacity, const Header &h, const uint8_t *stream, size_t length, size_t &written) {
    written = 0;
    if(!out || (!stream && length) || length > STREAM_CHUNK_MAX || capacity < HEADER_SIZE + length) return false;
    out[0] = MAGIC0; out[1] = MAGIC1; out[2] = VERSION;
    out[3] = static_cast<uint8_t>(h.session); out[4] = static_cast<uint8_t>(h.session >> 8);
    out[5] = static_cast<uint8_t>(h.sequence); out[6] = static_cast<uint8_t>(h.sequence >> 8);
    out[7] = static_cast<uint8_t>(length); out[8] = h.flags;
    if(length) memcpy(out + HEADER_SIZE, stream, length);
    written = HEADER_SIZE + length; return true;
}
bool decode(const uint8_t *packet, size_t packetLength, Header &h, const uint8_t *&stream) {
    if(!packet || packetLength < HEADER_SIZE || packet[0] != MAGIC0 || packet[1] != MAGIC1 || packet[2] != VERSION) return false;
    h.session = static_cast<uint16_t>(packet[3] | (packet[4] << 8));
    h.sequence = static_cast<uint16_t>(packet[5] | (packet[6] << 8)); h.length = packet[7]; h.flags = packet[8];
    if(h.length > STREAM_CHUNK_MAX || packetLength != HEADER_SIZE + h.length) return false;
    stream = packet + HEADER_SIZE; return true;
}
void SequenceTracker::reset() { initialized_ = false; }
SequenceResult SequenceTracker::observe(uint16_t session, uint16_t sequence, uint16_t &lost) {
    lost = 0;
    if(!initialized_) { initialized_ = true; session_ = session; last_ = sequence; return SequenceResult::FIRST; }
    if(session != session_) { session_ = session; last_ = sequence; return SequenceResult::SESSION_CHANGED; }
    const uint16_t delta = static_cast<uint16_t>(sequence - last_);
    if(delta == 0) return SequenceResult::DUPLICATE;
    if(delta < 0x8000U) { last_ = sequence; if(delta > 1) { lost = delta - 1; return SequenceResult::LOSS; } return SequenceResult::ACCEPT; }
    return SequenceResult::DUPLICATE;
}
}
