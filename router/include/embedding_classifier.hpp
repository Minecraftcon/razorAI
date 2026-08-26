#ifndef RAZOR_EMBEDDING_CLASSIFIER_HPP
#define RAZOR_EMBEDDING_CLASSIFIER_HPP

#include <string>
#include <vector>
#include <unordered_map>

namespace razor {

enum class Category {
    QuestionChat,
    SmallTask,
    Build,
    Unknown
};

std::string CategoryToString(Category category);
Category StringToCategory(const std::string& category_str);

class EmbeddingClassifier {
public:
    EmbeddingClassifier();
    ~EmbeddingClassifier() = default;

    // Fast deterministic feature vector generator (for lightweight routing without API calls)
    std::vector<float> GenerateEmbedding(const std::string& text) const;

    // Classify vector using reference centroids & keyword heuristic weighting
    Category Classify(const std::string& text, const std::vector<float>& embedding) const;

private:
    float CosineSimilarity(const std::vector<float>& v1, const std::vector<float>& v2) const;

    std::unordered_map<Category, std::vector<float>> centroids_;
};

} // namespace razor

#endif // RAZOR_EMBEDDING_CLASSIFIER_HPP
