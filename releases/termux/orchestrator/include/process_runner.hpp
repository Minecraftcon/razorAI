#ifndef RAZOR_PROCESS_RUNNER_HPP
#define RAZOR_PROCESS_RUNNER_HPP

#include <string>
#include <vector>
#include <chrono>

namespace razor {

struct ExecutionResult {
    int exit_code = -1;
    bool timed_out = false;
    std::string output;
    std::string error;
};

class ProcessRunner {
public:
    static ExecutionResult RunCommand(
        const std::vector<std::string>& args,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(300000),
        std::chrono::milliseconds grace_period = std::chrono::milliseconds(500)
    );

private:
    static void KillProcessGroup(pid_t pid, std::chrono::milliseconds grace_period);
};

} // namespace razor

#endif // RAZOR_PROCESS_RUNNER_HPP
