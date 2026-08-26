#ifndef RAZOR_UI_HPP
#define RAZOR_UI_HPP

#include <chrono>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <functional>

namespace razor {

using PromptCallback = std::function<void(const std::string&)>;

struct ChatMessage {
    std::string prompt;
    std::string response;
    bool is_loading = false;
    size_t streamed_length = 0;
    std::chrono::steady_clock::time_point start_time;
    std::string model_name;
};

class RazorUI {
public:
    RazorUI();
    ~RazorUI();

    void SetSubmitCallback(PromptCallback callback);
    void SetUserName(const std::string& user_name);
    void Run();

    // Call this to update the latest response (simulates streaming)
    void ProvideResponse(const std::string& response);
    
    // Starts a new thinking indicator with a timer
    void StartThinking(const std::string& model_name);
    
    // Updates the model name of the currently loading message
    void UpdateModelName(const std::string& model_name);
    
    void ProcessInput(const std::string& input);

    void SetSessionId(const std::string& sid) { current_session_id_ = sid; }
    std::string GetSessionId() const { return current_session_id_; }

    void SetAvailableModels(const std::vector<std::string>& models) { available_models_ = models; }
    int GetSelectedModelIndex() const { return selected_model_idx_.load(); }

    bool IsModelThinking() const;
    void DispatchQueuedSteer();

private:
    void Render();
    
    std::string current_session_id_ = "";
    std::string queued_steer_prompt_ = "";
    std::mutex steer_mutex_;
    std::vector<std::string> messages_;
    std::string current_input_;
    std::string prompt_value_;
    std::string user_name_;
    PromptCallback submit_callback_;

    std::vector<ChatMessage> history_;
    std::mutex history_mutex_;

    std::atomic<bool> is_running_;
    std::thread animation_thread_;
    std::atomic<int> spinner_frame_;

    // Inactivity timer
    std::atomic<std::chrono::steady_clock::time_point> last_keypress_;
    std::atomic<std::chrono::steady_clock::time_point> last_scroll_;
    
    std::atomic<bool> auto_scroll_;
    std::atomic<int> scroll_index_;
    
    std::atomic<int> selected_command_index_;

    // Model Picker State
    std::atomic<bool> show_model_picker_{false};
    std::vector<std::string> available_models_;
    std::atomic<int> selected_model_idx_{0};
    int model_menu_selected_ = 0;

    // Paste buffering state
    std::string pasted_buffer_ = "";
    int paste_line_count_ = 0;
    std::atomic<int> rapid_char_count_{0};
    std::atomic<bool> is_pasting_{false};
};

} // namespace razor

#endif // RAZOR_UI_HPP
