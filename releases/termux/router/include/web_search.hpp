#pragma once

#include <string>

namespace razor {

class WebSearch {
public:
    // Execute web search or url fetch via TinyFish API
    // - search_query: Web search query string (optional if fetch_url is given)
    // - fetch_url: URL to fetch and scrape clean text/markdown from (optional if search_query is given)
    // - api_key: TinyFish API Key (if empty, checks TINYFISH_API_KEY env or default config)
    static std::string Execute(const std::string& search_query, const std::string& fetch_url, const std::string& api_key = "");

    // Direct search helper
    static std::string Search(const std::string& query, const std::string& api_key = "");

    // Direct fetch/scrape helper
    static std::string Fetch(const std::string& url, const std::string& api_key = "");

    // Get active TinyFish API Key from config or environment
    static std::string GetApiKey();

    // Set active TinyFish API Key
    static void SetApiKey(const std::string& key);

private:
    static std::string UrlEncode(const std::string& value);
};

} // namespace razor
