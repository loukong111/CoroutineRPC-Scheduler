#include "corpcron/common/config.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

int main() {
    const std::string prefix = "/tmp/corpcron-config-test-" + std::to_string(getpid());
    const std::string first_path = prefix + "-first.conf";
    const std::string second_path = prefix + "-second.conf";

    {
        std::ofstream first(first_path);
        first << "[server]\nport = 8081\nstale = old\ninvalid = 12x\n";
    }
    {
        std::ofstream second(second_path);
        second << "[server]\nport = 8082\n";
    }

    auto& config = corpcron::Config::instance();
    assert(config.load(first_path));
    assert(config.getInt("server.port", 0) == 8081);
    assert(config.getInt("server.invalid", 99) == 99);
    assert(config.get("server.stale") == "old");

    assert(config.load(second_path));
    assert(config.getInt("server.port", 0) == 8082);
    assert(config.get("server.stale", "missing") == "missing");

    setenv("CORPCRON_SERVER_PORT", "9090", 1);
    assert(config.getInt("server.port", 0) == 9090);
    unsetenv("CORPCRON_SERVER_PORT");

    std::remove(first_path.c_str());
    std::remove(second_path.c_str());
    return 0;
}
