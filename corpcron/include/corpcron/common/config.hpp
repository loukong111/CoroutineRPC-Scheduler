#pragma once
#include <string>
#include <unordered_map>

namespace corpcron {

class Config {
public:
    static Config& instance();
    bool load(const std::string& filepath);
    std::string get(const std::string& section_key, const std::string& default_value = "") const;
    int getInt(const std::string& section_key, int default_value = 0) const;
private:
    std::unordered_map<std::string, std::string> kv_map_;
    void parseLine(const std::string& line, std::string& current_section);
};

}