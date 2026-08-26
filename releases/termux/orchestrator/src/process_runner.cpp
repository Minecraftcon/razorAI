#include "process_runner.hpp"

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>

#include <iostream>
#include <thread>
#include <array>
#include <cstring>

namespace razor {

void ProcessRunner::KillProcessGroup(pid_t pid, std::chrono::milliseconds grace_period) {
    if (pid <= 0) return;

    // Send SIGTERM to the whole process group
    kill(-pid, SIGTERM);

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < grace_period) {
        int status = 0;
        pid_t res = waitpid(pid, &status, WNOHANG);
        if (res == pid || res == -1) {
            return; // Exited cleanly
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Force kill remaining processes in group
    kill(-pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

ExecutionResult ProcessRunner::RunCommand(
    const std::vector<std::string>& args,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds grace_period
) {
    ExecutionResult result;
    if (args.empty()) {
        result.error = "Empty command arguments";
        return result;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result.error = "Failed to create pipe";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        result.error = "Failed to fork process";
        return result;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end

        // Set process group ID to pid so child process group is isolated
        setpgid(0, 0);

        // Redirect stdout and stderr to pipe
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Build C argv array
        std::vector<char*> c_argv;
        for (const auto& arg : args) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);

        execvp(c_argv[0], c_argv.data());
        // If execvp returns, it failed
        std::cerr << "execvp failed for " << args[0] << ": " << strerror(errno) << std::endl;
        _exit(127);
    }

    // Parent process
    close(pipefd[1]); // Close write end

    // Make read end non-blocking
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    auto start_time = std::chrono::steady_clock::now();
    bool process_done = false;
    std::array<char, 4096> buffer;

    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout) {
            result.timed_out = true;
            KillProcessGroup(pid, grace_period);
            break;
        }

        // Check if process has exited
        int status = 0;
        pid_t wait_res = waitpid(pid, &status, WNOHANG);
        if (wait_res == pid) {
            process_done = true;
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.exit_code = 128 + WTERMSIG(status);
            }
        }

        // Read available output from pipe
        struct pollfd pfd;
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        int poll_res = poll(&pfd, 1, 50); // 50ms timeout poll

        if (poll_res > 0 && (pfd.revents & POLLIN)) {
            ssize_t bytes_read;
            while ((bytes_read = read(pipefd[0], buffer.data(), buffer.size())) > 0) {
                result.output.append(buffer.data(), bytes_read);
            }
        }

        if (process_done) {
            // Drain remaining bytes from pipe
            ssize_t bytes_read;
            while ((bytes_read = read(pipefd[0], buffer.data(), buffer.size())) > 0) {
                result.output.append(buffer.data(), bytes_read);
            }
            break;
        }
    }

    close(pipefd[0]);
    return result;
}

} // namespace razor
