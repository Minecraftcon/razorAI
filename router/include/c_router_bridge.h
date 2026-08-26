#ifndef RAZOR_C_ROUTER_BRIDGE_H
#define RAZOR_C_ROUTER_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* RazorRouterHandle;

RazorRouterHandle RazorRouter_Create(size_t cache_capacity, int cache_ttl_sec);
void RazorRouter_Destroy(RazorRouterHandle handle);

int RazorRouter_Route(
    RazorRouterHandle handle,
    const char* prompt,
    char** out_category,
    int* out_cache_hit,
    float* out_confidence
);

size_t RazorRouter_GetCacheSize(RazorRouterHandle handle);
void RazorRouter_ClearCache(RazorRouterHandle handle);
void RazorRouter_FreeString(char* str);

#ifdef __cplusplus
}
#endif

#endif // RAZOR_C_ROUTER_BRIDGE_H
