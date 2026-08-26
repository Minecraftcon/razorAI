#include "embedding_classifier.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <cstdint>

namespace razor {

std::string CategoryToString(Category category) {
    switch (category) {
        case Category::QuestionChat: return "Question/Chat";
        case Category::SmallTask:   return "Small_Task";
        case Category::Build:       return "Build";
        default:                    return "Unknown";
    }
}

Category StringToCategory(const std::string& category_str) {
    if (category_str == "Question/Chat" || category_str == "Question") return Category::QuestionChat;
    if (category_str == "Small_Task" || category_str == "SmallTask")   return Category::SmallTask;
    if (category_str == "Build")                                       return Category::Build;
    return Category::Unknown;
}

EmbeddingClassifier::EmbeddingClassifier() {
    // Initialize representative centroids for Question/Chat, Small_Task, and Build
    centroids_[Category::QuestionChat] = GenerateEmbedding(
        "what is why how tell me explain describe difference can you details question chat help"
    );

    centroids_[Category::SmallTask] = GenerateEmbedding(
        "fix typo format code add comment update line small change rename refactor tweak patch task"
    );

    centroids_[Category::Build] = GenerateEmbedding(
        "build project create website setup database backend frontend app framework system architecture fullstack"
    );
}

std::vector<float> EmbeddingClassifier::GenerateEmbedding(const std::string& text) const {
    const size_t dim = 64;
    std::vector<float> vec(dim, 0.0f);

    std::string clean_text;
    clean_text.reserve(text.size());
    for (char c : text) {
        clean_text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    std::stringstream ss(clean_text);
    std::string word;
    size_t word_count = 0;

    while (ss >> word) {
        word_count++;
        // FNV-1a 32-bit hash for word feature distribution
        uint32_t hash = 2166136261u;
        for (char c : word) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }

        size_t idx = hash % dim;
        vec[idx] += 1.0f;
    }

    // Normalize vector
    float norm = 0.0f;
    for (float val : vec) {
        norm += val * val;
    }
    norm = std::sqrt(norm);

    if (norm > 0.0f) {
        for (float& val : vec) {
            val /= norm;
        }
    }

    return vec;
}

float EmbeddingClassifier::CosineSimilarity(const std::vector<float>& v1, const std::vector<float>& v2) const {
    if (v1.empty() || v1.size() != v2.size()) return 0.0f;
    double dot = 0.0;
    double norm1 = 0.0;
    double norm2 = 0.0;

    for (size_t i = 0; i < v1.size(); ++i) {
        dot += v1[i] * v2[i];
        norm1 += v1[i] * v1[i];
        norm2 += v2[i] * v2[i];
    }

    if (norm1 <= 0.0 || norm2 <= 0.0) return 0.0f;
    return static_cast<float>(dot / (std::sqrt(norm1) * std::sqrt(norm2)));
}

Category EmbeddingClassifier::Classify(const std::string& text, const std::vector<float>& embedding) const {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    // Rule-based heuristic weighting
    if (lower.find("create") != std::string::npos ||
        lower.find("build") != std::string::npos ||
        lower.find("website") != std::string::npos ||
        lower.find("framework") != std::string::npos ||
        lower.find("architecture") != std::string::npos ||
        lower.find("fullstack") != std::string::npos ||
        lower.find("app") != std::string::npos) {
        return Category::Build;
    }

    if (lower.find("what") != std::string::npos ||
        lower.find("why") != std::string::npos ||
        lower.find("how") != std::string::npos ||
        lower.find("explain") != std::string::npos ||
        lower.find("difference") != std::string::npos ||
        lower.find("tell me") != std::string::npos ||
        lower.find("can you") != std::string::npos) {
        return Category::QuestionChat;
    }

    // Cosine similarity matching with centroids
    float best_sim = -1.0f;
    Category best_cat = Category::SmallTask;

    for (const auto& pair : centroids_) {
        float sim = CosineSimilarity(embedding, pair.second);
        if (sim > best_sim) {
            best_sim = sim;
            best_cat = pair.first;
        }
    }

    return best_cat;
}

} // namespace razor
