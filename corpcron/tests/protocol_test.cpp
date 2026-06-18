#include "corpcron/rpc/protocol.hpp"
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>

int main() {
    uint32_t serial_id = 0;
    std::string payload;
    size_t frame_size = 0;

    std::string encoded = corpcron::rpc::encode(corpcron::rpc::kSubmitTaskRequestSerialId, "payload");
    assert(corpcron::rpc::tryDecodeFrame(encoded.data(), encoded.size(), serial_id, payload, frame_size) ==
           corpcron::rpc::DecodeStatus::Complete);
    assert(serial_id == corpcron::rpc::kSubmitTaskRequestSerialId);
    assert(payload == "payload");
    assert(frame_size == encoded.size());

    assert(corpcron::rpc::tryDecodeFrame(encoded.data(), 3, serial_id, payload, frame_size) ==
           corpcron::rpc::DecodeStatus::Incomplete);

    std::string malformed(8, '\0');
    malformed[3] = 3;
    assert(corpcron::rpc::tryDecodeFrame(malformed.data(), malformed.size(), serial_id, payload, frame_size) ==
           corpcron::rpc::DecodeStatus::Malformed);

    std::string too_large(8, '\0');
    uint32_t total_len = corpcron::rpc::kMaxFrameSize + 1;
    too_large[0] = static_cast<char>((total_len >> 24) & 0xFF);
    too_large[1] = static_cast<char>((total_len >> 16) & 0xFF);
    too_large[2] = static_cast<char>((total_len >> 8) & 0xFF);
    too_large[3] = static_cast<char>(total_len & 0xFF);
    assert(corpcron::rpc::tryDecodeFrame(too_large.data(), too_large.size(), serial_id, payload, frame_size) ==
           corpcron::rpc::DecodeStatus::TooLarge);

    std::string oversized(corpcron::rpc::kMaxPayloadSize + 1, 'x');
    std::string out;
    assert(!corpcron::rpc::tryEncode(corpcron::rpc::kSubmitTaskRequestSerialId, oversized, out));
    bool threw = false;
    try {
        (void)corpcron::rpc::encode(corpcron::rpc::kSubmitTaskRequestSerialId, oversized);
    } catch (const std::length_error&) {
        threw = true;
    }
    assert(threw);

    return 0;
}
