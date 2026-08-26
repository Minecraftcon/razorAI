#ifndef RAZOR_ROUTER_HPP
#define RAZOR_ROUTER_HPP

#include "context_cache.hpp"
#include "embedding_classifier.hpp"
#include <string>
#include <vector>
#include <memory>

namespace razor {

struct RouteResult {
    std::string category;      // "Question/Chat", "Small_Task", "Build"
    bool cache_hit = false;
    float confidence = 0.0f;
    std::string cache_key;
    std::string cached_response;
    std::vector<float> embedding;
};

class RouterEngine {
public:
    explicit RouterEngine(size_t cache_capacity = 1000, std::chrono::seconds cache_ttl = std::chrono::seconds(3600));
    ~RouterEngine() = default;

    RouteResult RoutePrompt(const std::string& prompt);

    void UpdateCacheResponse(const std::string& key, const std::vector<float>& embedding, const std::string& category, const std::string& response);

    // Cache operations
    size_t GetCacheSize() const;
    void ClearCache();

private:
    std::unique_ptr<ContextCache> cache_;
    std::unique_ptr<EmbeddingClassifier> classifier_;
};

} // namespace razor

#endif // RAZOR_ROUTER_HPP
