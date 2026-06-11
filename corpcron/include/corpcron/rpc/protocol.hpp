#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <cstring>

namespace corpcron {
namespace rpc {

constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kSerialIdSize = 4;
constexpr uint32_t kMaxFrameSize = 4 * 1024 * 1024;
constexpr uint32_t kRpcErrorSerialId = 100;

enum class DecodeStatus {
    Complete,
    Incomplete,
    Malformed,
    TooLarge
};

inline std::string encode(uint32_t serial_id, const std::string& payload) {
    uint32_t total_len = 4 + payload.size();
    std::string buffer;
    buffer.resize(4 + total_len);
    buffer[0] = (total_len >> 24) & 0xFF;
    buffer[1] = (total_len >> 16) & 0xFF;
    buffer[2] = (total_len >> 8) & 0xFF;
    buffer[3] = total_len & 0xFF;
    buffer[4] = (serial_id >> 24) & 0xFF;
    buffer[5] = (serial_id >> 16) & 0xFF;
    buffer[6] = (serial_id >> 8) & 0xFF;
    buffer[7] = serial_id & 0xFF;
    memcpy(&buffer[8], payload.data(), payload.size());
    return buffer;
}

inline DecodeStatus tryDecodeFrame(const char* data, size_t len, uint32_t& serial_id,
                                   std::string& payload, size_t& frame_size) {
    frame_size = 0;
    if (len < kHeaderSize) return DecodeStatus::Incomplete;

    uint32_t total_len = ((unsigned char)data[0] << 24) | ((unsigned char)data[1] << 16) |
                         ((unsigned char)data[2] << 8) | (unsigned char)data[3];
    if (total_len < kSerialIdSize) return DecodeStatus::Malformed;
    if (total_len > kMaxFrameSize) return DecodeStatus::TooLarge;

    frame_size = 4 + total_len;
    if (len < frame_size) return DecodeStatus::Incomplete;

    serial_id = ((unsigned char)data[4] << 24) | ((unsigned char)data[5] << 16) |
                ((unsigned char)data[6] << 8) | (unsigned char)data[7];
    payload.assign(data + kHeaderSize, total_len - kSerialIdSize);
    return DecodeStatus::Complete;
}

inline bool decode(const char* data, size_t len, uint32_t& serial_id, std::string& payload) {
    size_t frame_size = 0;
    return tryDecodeFrame(data, len, serial_id, payload, frame_size) == DecodeStatus::Complete;
}

} // namespace rpc
} // namespace corpcron
