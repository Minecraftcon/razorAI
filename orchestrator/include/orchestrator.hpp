#ifndef RAZOR_ORCHESTRATOR_HPP
#define RAZOR_ORCHESTRATOR_HPP

#include "config.hpp"
#include "process_runner.hpp"
#include "razor_ui.hpp"
#include <string>
#include <memory>

namespace razor {

class OrchestratorEngine {
public:
    explicit OrchestratorEngine(const std::string& config_path = "config.yaml");
    ~OrchestratorEngine() = default;

    int Run();

private:
    void HandlePrompt(const std::string& prompt);

    Config config_;
    std::unique_ptr<RazorUI> ui_;
};

} // namespace razor

#endif // RAZOR_ORCHESTRATOR_HPP
