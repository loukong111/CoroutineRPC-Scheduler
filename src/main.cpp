#include "corpcron/app/server_app.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string config_path = "config/server.conf";
    if (argc == 3 && std::string(argv[1]) == "--config") {
        config_path = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << " [--config <path>]\n";
        return 2;
    }
    corpcron::ServerApp app(config_path);
    return app.run();
}
