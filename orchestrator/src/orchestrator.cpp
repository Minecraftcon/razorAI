#include "orchestrator.hpp"
#include <iostream>
#include <vector>
#include <chrono>

namespace razor {

OrchestratorEngine::OrchestratorEngine(const std::string& config_path)
    : config_(Config::LoadConfig(config_path)),
      ui_(std::make_unique<RazorUI>()) {
    ui_->SetUserName(config_.user_name);
    ui_->SetSubmitCallback([this](const std::string& prompt) {
        this->HandlePrompt(prompt);
    });
}

void OrchestratorEngine::HandlePrompt(const std::string& prompt) {
    if (prompt.empty()) return;

    const ModelEntry* assigned_model = config_.models.empty() ? nullptr : &config_.models[0];

    if (assigned_model) {
        std::cout << "\n[RAZOR Engine] Using Model: "
                  << assigned_model->name << " (" << assigned_model->provider << "/"
                  << assigned_model->model << ")\n";
    }

    std::vector<std::string> args;
    if (!config_.agent_executable.empty()) {
        args = { config_.agent_executable, "--prompt", prompt };
    } else {
        // Shell fallback command for task execution
        args = { "/bin/sh", "-c", prompt };
    }

    auto timeout = std::chrono::seconds(config_.timeout_seconds);
    ExecutionResult result = ProcessRunner::RunCommand(args, timeout);

    if (result.timed_out) {
        std::cout << "\n[RAZOR Engine] Execution Timed Out after "
                  << config_.timeout_seconds << "s. Process group terminated cleanly.\n";
        return;
    }

    if (result.exit_code != 0) {
        std::cout << "\n[RAZOR Engine] Execution Error (Exit code: " << result.exit_code << ")\n"
                  << "Output:\n" << result.output << "\n";
        return;
    }

    std::cout << "\n--- RAZOR C++ Engine Response ---\n"
              << result.output << "\n";
}

int OrchestratorEngine::Run() {
    ui_->Run();
    return 0;
}

} // namespace razor
