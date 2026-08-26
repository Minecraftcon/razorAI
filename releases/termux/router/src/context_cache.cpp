#include "context_cache.hpp"
#include <cmath>
#include <algorithm>

namespace razor {

ContextCache::ContextCache(size_t capacity, std::chrono::seconds ttl)
    : capacity_(capacity), ttl_(ttl) {}

float ContextCache::CosineSimilarity(const std::vector<float>& v1, const std::vector<float>& v2) const {
    if (v1.empty() || v1.size() != v2.size()) return 0.0f;

    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;

    for (size_t i = 0; i < v1.size(); ++i) {
        dot += v1[i] * v2[i];
        norm_a += v1[i] * v1[i];
        norm_b += v2[i] * v2[i];
    }

    if (norm_a <= 0.0 || norm_b <= 0.0) return 0.0f;
    return static_cast<float>(dot / (std::sqrt(norm_a) * std::sqrt(norm_b)));
}

void ContextCache::Put(const std::string& key, const std::vector<float>& embedding, const std::string& category, const std::string& cached_response) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        // Update existing
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        (*it->second)->timestamp = std::chrono::steady_clock::now();
        (*it->second)->cached_response = cached_response;
        return;
    }

    if (cache_list_.size() >= capacity_) {
        // Evict LRU
        auto last = cache_list_.back();
        cache_map_.erase(last->key);
        cache_list_.pop_back();
    }

    auto entry = std::make_shared<ContextEntry>();
    entry->key = key;
    entry->embedding = embedding;
    entry->category = category;
    entry->cached_response = cached_response;
    entry->timestamp = std::chrono::steady_clock::now();
    entry->hit_count = 0;

    cache_list_.push_front(entry);
    cache_map_[key] = cache_list_.begin();
}

std::shared_ptr<ContextEntry> ContextCache::Get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
        return nullptr;
    }

    // Check TTL expiration
    auto now = std::chrono::steady_clock::now();
    if (now - it->second->get()->timestamp > ttl_) {
        cache_list_.erase(it->second);
        cache_map_.erase(it);
        return nullptr;
    }

    // Hit: move to front & increment count
    it->second->get()->hit_count++;
    cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
    return *(it->second);
}

std::shared_ptr<ContextEntry> ContextCache::FindSimilar(const std::vector<float>& query_embedding, float similarity_threshold) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (query_embedding.empty()) return nullptr;

    auto now = std::chrono::steady_clock::now();
    float best_sim = -1.0f;
    ListIter best_it = cache_list_.end();

    for (auto it = cache_list_.begin(); it != cache_list_.end(); ++it) {
        if (now - (*it)->timestamp > ttl_) continue;

        float sim = CosineSimilarity(query_embedding, (*it)->embedding);
        if (sim >= similarity_threshold && sim > best_sim) {
            best_sim = sim;
            best_it = it;
        }
    }

    if (best_it != cache_list_.end()) {
        (*best_it)->hit_count++;
        cache_list_.splice(cache_list_.begin(), cache_list_, best_it);
        return *best_it;
    }

    return nullptr;
}

size_t ContextCache::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_map_.size();
}

void ContextCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_map_.clear();
    cache_list_.clear();
}

void ContextCache::EvictExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto it = cache_list_.begin(); it != cache_list_.end(); ) {
        if (now - (*it)->timestamp > ttl_) {
            cache_map_.erase((*it)->key);
            it = cache_list_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace razor
