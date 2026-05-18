#pragma once
#include <cstdint>
#include <string>
#include <cstring>

namespace corpcron {
namespace rpc {

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

inline bool decode(const char* data, size_t len, uint32_t& serial_id, std::string& payload) {
    if (len < 8) return false;
    uint32_t total_len = ((unsigned char)data[0] << 24) | ((unsigned char)data[1] << 16) |
                         ((unsigned char)data[2] << 8) | (unsigned char)data[3];
    if (len < 4 + total_len) return false;
    serial_id = ((unsigned char)data[4] << 24) | ((unsigned char)data[5] << 16) |
                ((unsigned char)data[6] << 8) | (unsigned char)data[7];
    payload.assign(data + 8, total_len - 4);
    return true;
}

} // namespace rpc
} // namespace corpcron