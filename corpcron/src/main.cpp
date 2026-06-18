#include "corpcron/app/server_app.hpp"
#include <string>

int main(int argc, char* argv[]) {
    std::string config_path = "config/server.conf";
    if (argc >= 2 && std::string(argv[1]) == "--config" && argc >= 3) {
        config_path = argv[2];
    }
    corpcron::ServerApp app(config_path);
    return app.run();
}
