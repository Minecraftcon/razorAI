#include "task_manager.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <poll.h>

namespace razor {

TaskManager& TaskManager::Instance() {
    static TaskManager instance;
    return instance;
}

TaskManager::~TaskManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : tasks_) {
        TaskInfo& t = pair.second;
        if (t.status == TaskStatus::RUNNING && t.pid > 0) {
            kill(-t.pid, SIGTERM);
            if (t.stdin_fd >= 0) close(t.stdin_fd);
        }
        if (t.monitor_thread && t.monitor_thread->joinable()) {
            t.monitor_thread->detach();
        }
    }
}

std::string TaskManager::GenerateTaskId() {
    return "task-" + std::to_string(task_counter_++);
}

void TaskManager::EnsureLogDir(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        std::string dir = path.substr(0, pos);
        std::string cmd = "mkdir -p \"" + dir + "\"";
        int ret = system(cmd.c_str());
        (void)ret;
    }
}

TaskLaunchResult TaskManager::LaunchTask(const std::string& session_id,
                                         const std::string& command,
                                         const std::string& name,
                                         int work_time_secs) {
    std::string task_id = GenerateTaskId();
    std::string task_name = name.empty() ? ("cmd_" + task_id) : name;

    const char* home = std::getenv("HOME");
    std::string base_dir = home ? (std::string(home) + "/.razor/sessions") : "/tmp/razor_sessions";
    std::string log_dir = session_id.empty() ? ("/tmp/razor_tasks") : (base_dir + "/" + session_id + "/logs");
    std::string log_path = log_dir + "/" + task_id + ".log";
    EnsureLogDir(log_path);

    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        TaskLaunchResult res;
        res.is_background = false;
        res.task_id = task_id;
        res.name = task_name;
        res.exit_code = -1;
        res.output = "Error: Failed to create pipes for process execution.";
        return res;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        TaskLaunchResult res;
        res.is_background = false;
        res.task_id = task_id;
        res.name = task_name;
        res.exit_code = -1;
        res.output = "Error: Failed to fork process.";
        return res;
    }

    if (pid == 0) {
        // Child Process
        setpgid(0, 0); // New process group

        close(stdin_pipe[1]);  // Close write end of stdin
        close(stdout_pipe[0]); // Close read end of stdout

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        execl("/bin/bash", "bash", "-c", command.c_str(), (char*)NULL);
        _exit(127);
    }

    // Parent Process
    close(stdin_pipe[0]);  // Close read end of stdin
    close(stdout_pipe[1]); // Close write end of stdout

    int out_read_fd = stdout_pipe[0];
    int in_write_fd = stdin_pipe[1];

    // Set out_read_fd to non-blocking
    int flags = fcntl(out_read_fd, F_GETFL, 0);
    fcntl(out_read_fd, F_SETFL, flags | O_NONBLOCK);

    auto start_time = std::chrono::steady_clock::now();
    std::string sync_output;
    bool finished_sync = false;
    int child_status = 0;

    int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    auto drain_output = [&]() {
        char buf[4096];
        ssize_t n;
        while ((n = read(out_read_fd, buf, sizeof(buf))) > 0) {
            sync_output.append(buf, n);
            if (log_fd >= 0) {
                ssize_t written = write(log_fd, buf, n);
                (void)written;
            }
        }
    };

    int timeout_ms = (work_time_secs <= 0 ? 5 : work_time_secs) * 1000;
    auto deadline = start_time + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        drain_output();

        pid_t wait_res = waitpid(pid, &child_status, WNOHANG);
        if (wait_res == pid) {
            finished_sync = true;
            drain_output();
            break;
        } else if (wait_res < 0) {
            finished_sync = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (log_fd >= 0) {
        close(log_fd);
    }

    if (finished_sync) {
        close(out_read_fd);
        close(in_write_fd);

        int exit_code = 0;
        if (WIFEXITED(child_status)) {
            exit_code = WEXITSTATUS(child_status);
        } else if (WIFSIGNALED(child_status)) {
            exit_code = 128 + WTERMSIG(child_status);
        }

        TaskInfo info;
        info.task_id = task_id;
        info.name = task_name;
        info.command = command;
        info.session_id = session_id;
        info.pid = pid;
        info.stdin_fd = -1;
        info.log_path = log_path;
        info.start_time = start_time;
        info.end_time = std::chrono::steady_clock::now();
        info.status = (exit_code == 0) ? TaskStatus::DONE : TaskStatus::FAILED;
        info.exit_code = exit_code;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_[task_id] = info;
        }

        TaskLaunchResult res;
        res.is_background = false;
        res.task_id = task_id;
        res.name = task_name;
        res.exit_code = exit_code;
        res.output = sync_output;
        res.log_path = log_path;
        return res;
    }

    // Process is still running -> Background it!
    TaskInfo info;
    info.task_id = task_id;
    info.name = task_name;
    info.command = command;
    info.session_id = session_id;
    info.pid = pid;
    info.stdin_fd = in_write_fd;
    info.log_path = log_path;
    info.start_time = start_time;
    info.status = TaskStatus::RUNNING;
    info.exit_code = 0;

    // Launch background thread to continue streaming output to log file and await process exit
    info.monitor_thread = std::make_shared<std::thread>([this, task_id, pid, out_read_fd, log_path]() {
        int fd_out = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        char buf[4096];
        int status = 0;

        while (true) {
            ssize_t n = read(out_read_fd, buf, sizeof(buf));
            if (n > 0) {
                if (fd_out >= 0) {
                    ssize_t written = write(fd_out, buf, n);
                    (void)written;
                }
            } else if (n == 0) {
                // EOF reached
                break;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    break;
                }
            }

            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid || r < 0) {
                // Drain any final bytes
                while ((n = read(out_read_fd, buf, sizeof(buf))) > 0) {
                    if (fd_out >= 0) {
                        ssize_t written = write(fd_out, buf, n);
                        (void)written;
                    }
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (fd_out >= 0) close(fd_out);
        close(out_read_fd);

        int exit_code = 0;
        if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tasks_.find(task_id);
            if (it != tasks_.end()) {
                if (it->second.status == TaskStatus::RUNNING) {
                    it->second.status = (exit_code == 0) ? TaskStatus::DONE : TaskStatus::FAILED;
                }
                it->second.exit_code = exit_code;
                it->second.end_time = std::chrono::steady_clock::now();
                if (it->second.stdin_fd >= 0) {
                    close(it->second.stdin_fd);
                    it->second.stdin_fd = -1;
                }
            }
        }
    });

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_[task_id] = info;
    }

    TaskLaunchResult res;
    res.is_background = true;
    res.task_id = task_id;
    res.name = task_name;
    res.exit_code = 0;
    res.log_path = log_path;
    res.output = "Command exceeded work_time (" + std::to_string(work_time_secs) + "s) and was sent to background as Task '" +
                 task_name + "' (ID: " + task_id + ").\nOutput is streaming to: " + log_path +
                 "\nUse manage_task with action='view', 'send_keycode', or 'kill' to interact with this task.";
    return res;
}

std::string TaskManager::ViewTask(const std::string& task_id, int max_lines) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return "Error: Task ID '" + task_id + "' not found.";
    }

    const TaskInfo& t = it->second;
    auto now = std::chrono::steady_clock::now();
    double runtime = std::chrono::duration<double>(
        (t.status == TaskStatus::RUNNING ? now : t.end_time) - t.start_time
    ).count();

    std::string status_str = "RUNNING";
    if (t.status == TaskStatus::DONE) status_str = "DONE (exit " + std::to_string(t.exit_code) + ")";
    else if (t.status == TaskStatus::FAILED) status_str = "FAILED (exit " + std::to_string(t.exit_code) + ")";
    else if (t.status == TaskStatus::KILLED) status_str = "KILLED";

    std::ostringstream ss;
    ss << "=== Task Details: " << t.name << " (" << t.task_id << ") ===\n";
    ss << "Status: " << status_str << "\n";
    ss << "Runtime: " << std::fixed << std::setprecision(1) << runtime << "s\n";
    ss << "Command: " << t.command << "\n";
    ss << "Log Path: " << t.log_path << "\n\n";
    ss << "--- Recent Output ---\n";

    std::ifstream file(t.log_path);
    if (!file.is_open()) {
        ss << "(No log file available)\n";
    } else {
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        size_t start_idx = 0;
        if ((int)lines.size() > max_lines) {
            start_idx = lines.size() - max_lines;
            ss << "[... truncated " << start_idx << " earlier lines ...]\n";
        }
        for (size_t i = start_idx; i < lines.size(); ++i) {
            ss << lines[i] << "\n";
        }
    }
    return ss.str();
}

bool TaskManager::KillTask(const std::string& task_id, std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        message = "Error: Task ID '" + task_id + "' not found.";
        return false;
    }

    TaskInfo& t = it->second;
    if (t.status != TaskStatus::RUNNING || t.pid <= 0) {
        message = "Task '" + task_id + "' is not running (Current status: " +
                  (t.status == TaskStatus::DONE ? "DONE" : (t.status == TaskStatus::FAILED ? "FAILED" : "KILLED")) + ").";
        return false;
    }

    kill(-t.pid, SIGTERM);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    kill(-t.pid, SIGKILL);

    t.status = TaskStatus::KILLED;
    t.end_time = std::chrono::steady_clock::now();
    if (t.stdin_fd >= 0) {
        close(t.stdin_fd);
        t.stdin_fd = -1;
    }

    message = "Successfully killed task '" + t.name + "' (" + task_id + ").";
    return true;
}

bool TaskManager::SendKeycode(const std::string& task_id, const std::string& input, std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        message = "Error: Task ID '" + task_id + "' not found.";
        return false;
    }

    TaskInfo& t = it->second;
    if (t.status != TaskStatus::RUNNING || t.stdin_fd < 0) {
        message = "Error: Task '" + task_id + "' is not actively running with an open stdin channel.";
        return false;
    }

    ssize_t written = write(t.stdin_fd, input.data(), input.size());
    if (written < 0) {
        message = "Error: Failed to write to task stdin (errno: " + std::to_string(errno) + ").";
        return false;
    }

    message = "Sent " + std::to_string(written) + " bytes to task '" + task_id + "'.";
    return true;
}

std::string TaskManager::FormatTaskTable(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const TaskInfo*> relevant;
    for (const auto& pair : tasks_) {
        if (session_id.empty() || pair.second.session_id == session_id || pair.second.session_id.empty()) {
            relevant.push_back(&pair.second);
        }
    }

    if (relevant.empty()) {
        return "";
    }

    auto now = std::chrono::steady_clock::now();
    std::ostringstream ss;
    ss << "[Tasks:]\n";
    ss << std::left << std::setw(20) << "Name"
       << std::left << std::setw(12) << "Id"
       << std::left << std::setw(12) << "Status"
       << "Running Time\n";

    for (const auto* t : relevant) {
        double duration = std::chrono::duration<double>(
            (t->status == TaskStatus::RUNNING ? now : t->end_time) - t->start_time
        ).count();

        std::string status_str = "RUNNING";
        if (t->status == TaskStatus::DONE) status_str = "DONE";
        else if (t->status == TaskStatus::FAILED) status_str = "FAILED";
        else if (t->status == TaskStatus::KILLED) status_str = "KILLED";

        std::ostringstream time_str;
        time_str << "[" << std::fixed << std::setprecision(1) << duration << "s]";

        ss << std::left << std::setw(20) << t->name.substr(0, 19)
           << std::left << std::setw(12) << t->task_id
           << std::left << std::setw(12) << status_str
           << time_str.str() << "\n";
    }
    return ss.str();
}

std::string TaskManager::GetTaskName(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end() && !it->second.name.empty()) {
        return it->second.name;
    }
    return task_id;
}

std::vector<TaskInfo> TaskManager::GetSessionTasks(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskInfo> list;
    for (const auto& pair : tasks_) {
        if (session_id.empty() || pair.second.session_id == session_id) {
            list.push_back(pair.second);
        }
    }
    return list;
}

} // namespace razor
