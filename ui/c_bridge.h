#ifndef RAZOR_C_BRIDGE_H
#define RAZOR_C_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RazorPromptCallback)(const char* prompt);

void* RazorUI_Create();
void RazorUI_Destroy(void* handle);
void RazorUI_SetSubmitCallback(void* handle, RazorPromptCallback cb);
void RazorUI_SetUserName(void* handle, const char* user_name);
void RazorUI_Run(void* handle);

#ifdef __cplusplus
}
#endif

#endif // RAZOR_C_BRIDGE_H
