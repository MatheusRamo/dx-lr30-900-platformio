#pragma once
#include <stddef.h>
#include <stdint.h>

class Rtcm3Parser {
public:
    static constexpr size_t MAX_FRAME_SIZE = 1029;
    using Callback = void (*)(void *context, const uint8_t *frame, size_t length,
                              uint16_t type, bool crcValid);

    Rtcm3Parser(Callback callback = nullptr, void *context = nullptr);
    void setCallback(Callback callback, void *context);
    void feed(const uint8_t *data, size_t length);
    void feed(uint8_t byte);
    void reset();
    size_t buffered() const { return used_; }

private:
    void process();
    void resync();
    uint8_t buffer_[MAX_FRAME_SIZE];
    size_t used_ = 0;
    Callback callback_;
    void *context_;
};
