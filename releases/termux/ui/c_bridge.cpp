#include "c_bridge.h"
#include "razor_ui.hpp"

extern "C" {

void* RazorUI_Create() {
    return new razor::RazorUI();
}

void RazorUI_Destroy(void* handle) {
    if (handle) {
        delete static_cast<razor::RazorUI*>(handle);
    }
}

void RazorUI_SetSubmitCallback(void* handle, RazorPromptCallback cb) {
    if (handle && cb) {
        auto* ui = static_cast<razor::RazorUI*>(handle);
        ui->SetSubmitCallback([cb](const std::string& prompt) {
            cb(prompt.c_str());
        });
    }
}

void RazorUI_SetUserName(void* handle, const char* user_name) {
    if (handle && user_name) {
        auto* ui = static_cast<razor::RazorUI*>(handle);
        ui->SetUserName(user_name);
    }
}

void RazorUI_Run(void* handle) {
    if (handle) {
        auto* ui = static_cast<razor::RazorUI*>(handle);
        ui->Run();
    }
}

}
