#include "c_router_bridge.h"
#include "router.hpp"
#include <cstdlib>
#include <cstring>

extern "C" {

RazorRouterHandle RazorRouter_Create(size_t cache_capacity, int cache_ttl_sec) {
    auto* router = new (std::nothrow) razor::RouterEngine(
        cache_capacity,
        std::chrono::seconds(cache_ttl_sec > 0 ? cache_ttl_sec : 3600)
    );
    return static_cast<RazorRouterHandle>(router);
}

void RazorRouter_Destroy(RazorRouterHandle handle) {
    if (handle) {
        auto* router = static_cast<razor::RouterEngine*>(handle);
        delete router;
    }
}

int RazorRouter_Route(
    RazorRouterHandle handle,
    const char* prompt,
    char** out_category,
    int* out_cache_hit,
    float* out_confidence
) {
    if (!handle || !prompt || !out_category || !out_cache_hit || !out_confidence) {
        return -1;
    }

    auto* router = static_cast<razor::RouterEngine*>(handle);
    razor::RouteResult result = router->RoutePrompt(prompt);

    *out_category = strdup(result.category.c_str());
    *out_cache_hit = result.cache_hit ? 1 : 0;
    *out_confidence = result.confidence;

    return 0;
}

size_t RazorRouter_GetCacheSize(RazorRouterHandle handle) {
    if (!handle) return 0;
    auto* router = static_cast<razor::RouterEngine*>(handle);
    return router->GetCacheSize();
}

void RazorRouter_ClearCache(RazorRouterHandle handle) {
    if (handle) {
        auto* router = static_cast<razor::RouterEngine*>(handle);
        router->ClearCache();
    }
}

void RazorRouter_FreeString(char* str) {
    if (str) {
        free(str);
    }
}

}
