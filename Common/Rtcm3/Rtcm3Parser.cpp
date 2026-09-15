#include "Rtcm3Parser.h"
#include "Crc24Q.h"
#include <string.h>

Rtcm3Parser::Rtcm3Parser(Callback callback, void *context) : callback_(callback), context_(context) {}
void Rtcm3Parser::setCallback(Callback callback, void *context) { callback_ = callback; context_ = context; }
void Rtcm3Parser::reset() { used_ = 0; }

void Rtcm3Parser::feed(const uint8_t *data, size_t length) {
    for(size_t i = 0; i < length; ++i) feed(data[i]);
}

void Rtcm3Parser::feed(uint8_t byte) {
    if(used_ == 0 && byte != 0xD3) return;
    if(used_ >= MAX_FRAME_SIZE) reset();
    if(used_ == 0 && byte != 0xD3) return;
    buffer_[used_++] = byte;
    process();
}

void Rtcm3Parser::resync() {
    size_t next = 1;
    while(next < used_ && buffer_[next] != 0xD3) ++next;
    if(next >= used_) { used_ = 0; return; }
    memmove(buffer_, buffer_ + next, used_ - next);
    used_ -= next;
}

void Rtcm3Parser::process() {
    while(used_ >= 3) {
        if(buffer_[0] != 0xD3) { resync(); continue; }
        if((buffer_[1] & 0xFCU) != 0) { resync(); continue; }
        const size_t payloadLength = (static_cast<size_t>(buffer_[1] & 0x03U) << 8) | buffer_[2];
        const size_t frameLength = payloadLength + 6U;
        if(frameLength > MAX_FRAME_SIZE) { resync(); continue; }
        if(used_ < frameLength) return;
        const uint32_t expected = crc24q(buffer_, frameLength - 3U);
        const uint32_t received = (static_cast<uint32_t>(buffer_[frameLength - 3]) << 16) |
                                  (static_cast<uint32_t>(buffer_[frameLength - 2]) << 8) |
                                   buffer_[frameLength - 1];
        const uint16_t type = payloadLength >= 2 ?
            static_cast<uint16_t>(((buffer_[3] << 4) | (buffer_[4] >> 4)) & 0x0FFFU) : 0;
        const bool valid = expected == received;
        if(callback_) callback_(context_, buffer_, frameLength, type, valid);
        if(valid) {
            memmove(buffer_, buffer_ + frameLength, used_ - frameLength);
            used_ -= frameLength;
        } else {
            resync();
        }
    }
}
