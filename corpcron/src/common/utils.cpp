#include "corpcron/utils.hpp"
#include <algorithm>
#include <cctype>

namespace corpcron {
std::string trim(const std::string& text) {
    auto start = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    return (start < end) ? std::string(start, end) : "";
}
} // namespace corpcron