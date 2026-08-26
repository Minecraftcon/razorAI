#ifndef RAZOR_CONTEXT_CACHE_HPP
#define RAZOR_CONTEXT_CACHE_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>

namespace razor {

struct ContextEntry {
    std::string key;
    std::vector<float> embedding;
    std::string category; // "Question/Chat", "Small_Task", "Build"
    std::string cached_response;
    std::chrono::steady_clock::time_point timestamp;
    size_t hit_count = 0;
};

class ContextCache {
public:
    explicit ContextCache(size_t capacity = 1000, std::chrono::seconds ttl = std::chrono::seconds(3600));
    ~ContextCache() = default;

    // Disallow copy to prevent accidental resource duplicates
    ContextCache(const ContextCache&) = delete;
    ContextCache& operator=(const ContextCache&) = delete;

    void Put(const std::string& key, const std::vector<float>& embedding, const std::string& category, const std::string& cached_response = "");
    std::shared_ptr<ContextEntry> Get(const std::string& key);
    std::shared_ptr<ContextEntry> FindSimilar(const std::vector<float>& query_embedding, float similarity_threshold = 0.88f);

    size_t Size() const;
    void Clear();
    void EvictExpired();

private:
    float CosineSimilarity(const std::vector<float>& v1, const std::vector<float>& v2) const;

    size_t capacity_;
    std::chrono::seconds ttl_;
    mutable std::mutex mutex_;

    using ListIter = std::list<std::shared_ptr<ContextEntry>>::iterator;
    std::list<std::shared_ptr<ContextEntry>> cache_list_;
    std::unordered_map<std::string, ListIter> cache_map_;
};

} // namespace razor

#endif // RAZOR_CONTEXT_CACHE_HPP
