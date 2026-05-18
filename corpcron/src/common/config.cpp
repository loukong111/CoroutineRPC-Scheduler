#include "corpcron/common/config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace corpcron {

Config& Config::instance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    std::string line;
    std::string current_section;
    while (std::getline(file, line)) {
        parseLine(line, current_section);
    }
    return true;
}

void Config::parseLine(const std::string& line, std::string& current_section) {
    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    if (trimmed.empty() || trimmed[0] == '#') return;
    if (trimmed[0] == '[') {
        size_t end = trimmed.find(']');
        if (end != std::string::npos) {
            current_section = trimmed.substr(1, end - 1);
        }
        return;
    }
    size_t eq = trimmed.find('=');
    if (eq != std::string::npos) {
        std::string key = trimmed.substr(0, eq);
        std::string value = trimmed.substr(eq + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        if (!current_section.empty()) {
            kv_map_[current_section + "." + key] = value;
        } else {
            kv_map_[key] = value;
        }
    }
}

std::string Config::get(const std::string& section_key, const std::string& default_value) const {
    auto it = kv_map_.find(section_key);
    if (it != kv_map_.end()) return it->second;
    return default_value;
}

int Config::getInt(const std::string& section_key, int default_value) const {
    std::string val = get(section_key, "");
    if (val.empty()) return default_value;
    try {
        return std::stoi(val);
    } catch (...) {
        return default_value;
    }
}

}