#include "corpcron/app/worker_app.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string config_path = "config/worker.conf";
    if (argc == 3 && std::string(argv[1]) == "--config") {
        config_path = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << " [--config <path>]\n";
        return 2;
    }
    corpcron::WorkerApp app(config_path);
    return app.run();
}
