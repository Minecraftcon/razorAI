#ifndef RAZOR_TASK_MANAGER_HPP
#define RAZOR_TASK_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>

namespace razor {

enum class TaskStatus {
    RUNNING,
    DONE,
    FAILED,
    KILLED
};

struct TaskInfo {
    std::string task_id;
    std::string name;
    std::string command;
    std::string session_id;
    pid_t pid = -1;
    int stdin_fd = -1;
    std::string log_path;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    TaskStatus status = TaskStatus::RUNNING;
    int exit_code = 0;
    std::shared_ptr<std::thread> monitor_thread;
};

struct TaskLaunchResult {
    bool is_background = false;
    std::string task_id;
    std::string name;
    int exit_code = 0;
    std::string output;
    std::string log_path;
};

class TaskManager {
public:
    static TaskManager& Instance();

    // Launch a command. If it finishes within work_time_secs, returns synchronous output.
    // Otherwise, backgrounds the task and returns background info with task_id.
    TaskLaunchResult LaunchTask(const std::string& session_id,
                                const std::string& command,
                                const std::string& name = "",
                                int work_time_secs = 5);

    // View task status and recent log output
    std::string ViewTask(const std::string& task_id, int max_lines = 100);

    // Kill a running task
    bool KillTask(const std::string& task_id, std::string& message);

    // Send input / keycodes to a running task's stdin
    bool SendKeycode(const std::string& task_id, const std::string& input, std::string& message);

    // Get task readable name by task_id
    std::string GetTaskName(const std::string& task_id);

    // Format active/recent tasks into a [Tasks:] table for prompt context
    std::string FormatTaskTable(const std::string& session_id);

    // Get list of tasks for a session
    std::vector<TaskInfo> GetSessionTasks(const std::string& session_id);

private:
    TaskManager() = default;
    ~TaskManager();

    std::string GenerateTaskId();
    void EnsureLogDir(const std::string& path);

    mutable std::mutex mutex_;
    std::map<std::string, TaskInfo> tasks_;
    std::atomic<uint64_t> task_counter_{1};
};

} // namespace razor

#endif // RAZOR_TASK_MANAGER_HPP
