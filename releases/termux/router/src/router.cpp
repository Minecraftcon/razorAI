#include "router.hpp"
#include <sstream>
#include <iomanip>
#include <cstdint>

namespace razor {

static std::string HashString(const std::string& str) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ull;
    }
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

RouterEngine::RouterEngine(size_t cache_capacity, std::chrono::seconds cache_ttl)
    : cache_(std::make_unique<ContextCache>(cache_capacity, cache_ttl)),
      classifier_(std::make_unique<EmbeddingClassifier>()) {}

RouteResult RouterEngine::RoutePrompt(const std::string& prompt) {
    RouteResult res;
    if (prompt.empty()) {
        res.category = "Question/Chat";
        res.confidence = 1.0f;
        return res;
    }

    std::string key = HashString(prompt);
    res.cache_key = key;

    // 1. Direct exact key match lookup in Context Cache
    auto cached_entry = cache_->Get(key);
    if (cached_entry) {
        res.category = cached_entry->category;
        res.cache_hit = true;
        res.confidence = 1.0f;
        res.embedding = cached_entry->embedding;
        res.cached_response = cached_entry->cached_response;
        return res;
    }

    // 2. Generate vector embedding
    res.embedding = classifier_->GenerateEmbedding(prompt);

    // 3. Context Cache similarity search (semantic match lookup)
    auto similar_entry = cache_->FindSimilar(res.embedding, 0.88f);
    if (similar_entry) {
        res.category = similar_entry->category;
        res.cache_hit = true;
        res.confidence = 0.92f;
        res.cached_response = similar_entry->cached_response;
        return res;
    }

    // 4. Full classification
    Category cat = classifier_->Classify(prompt, res.embedding);
    res.category = CategoryToString(cat);
    res.cache_hit = false;
    res.confidence = 0.85f;

    // 5. Store result in Context Cache
    cache_->Put(key, res.embedding, res.category);

    return res;
}

size_t RouterEngine::GetCacheSize() const {
    return cache_ ? cache_->Size() : 0;
}

void RouterEngine::ClearCache() {
    if (cache_) {
        cache_->Clear();
    }
}

void RouterEngine::UpdateCacheResponse(const std::string& key, const std::vector<float>& embedding, const std::string& category, const std::string& response) {
    if (cache_) {
        cache_->Put(key, embedding, category, response);
    }
}

} // namespace razor
