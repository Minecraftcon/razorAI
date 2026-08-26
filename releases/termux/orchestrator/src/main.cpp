#include "orchestrator.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    std::string config_path = "config.yaml";
    if (argc > 1) {
        config_path = argv[1];
    }

    try {
        razor::OrchestratorEngine engine(config_path);
        return engine.Run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal Orchestrator Error: " << ex.what() << std::endl;
        return 1;
    }
}
