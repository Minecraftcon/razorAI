#include "web_search.hpp"
#include "http_client.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace razor {

static std::string g_tinyfish_api_key = "sk-tinyfish--bB3m22tB_FZdRCBFz4m47HDq0OwuM-E";
static std::mutex g_key_mutex;

std::string WebSearch::GetApiKey() {
    std::lock_guard<std::mutex> lock(g_key_mutex);
    const char* env_k = std::getenv("TINYFISH_API_KEY");
    if (env_k && std::string(env_k).length() > 0) {
        return std::string(env_k);
    }
    return g_tinyfish_api_key;
}

void WebSearch::SetApiKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_key_mutex);
    if (!key.empty()) {
        g_tinyfish_api_key = key;
    }
}

std::string WebSearch::UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else if (c == ' ') {
            escaped << '+';
        } else {
            escaped << '%' << std::setw(2) << ((int)(unsigned char)c);
        }
    }
    return escaped.str();
}

std::string WebSearch::Search(const std::string& query, const std::string& api_key) {
    std::string key = api_key.empty() ? GetApiKey() : api_key;
    if (query.empty()) {
        return "Error: Empty search query provided.";
    }

    std::string endpoint = "https://api.search.tinyfish.ai?query=" + UrlEncode(query) + "&location=US&language=en";
    std::vector<std::string> headers = {
        "X-API-Key: " + key,
        "Accept: application/json"
    };

    std::string response = HttpClient::Request("GET", endpoint, headers, "", 15);
    if (response.empty()) {
        return "Error: Failed to connect to TinyFish Search API or empty response.";
    }

    try {
        json j = json::parse(response);
        if (j.contains("error")) {
            return "TinyFish API Error: " + j["error"].dump();
        }

        std::ostringstream out;
        out << "### Web Search Results for: `" << query << "`\n\n";

        if (j.contains("results") && j["results"].is_array() && !j["results"].empty()) {
            int rank = 1;
            for (const auto& item : j["results"]) {
                std::string title = item.value("title", "No Title");
                std::string url = item.value("url", "");
                std::string snippet = item.value("snippet", "");
                std::string site = item.value("site_name", "");

                out << rank << ". **[" << title << "](" << url << ")**\n";
                if (!site.empty()) {
                    out << "   *Source*: " << site << "\n";
                }
                if (!snippet.empty()) {
                    out << "   *Snippet*: " << snippet << "\n";
                }
                out << "\n";
                rank++;
            }
            if (j.contains("total_results")) {
                out << "*Total results found: " << j["total_results"].dump() << "*\n";
            }
        } else {
            out << "No web search results found for query: " << query << "\n";
        }
        return out.str();
    } catch (const std::exception& e) {
        return "Error parsing TinyFish Search JSON response: " + std::string(e.what()) + "\nRaw response: " + response.substr(0, 500);
    }
}

std::string WebSearch::Fetch(const std::string& url, const std::string& api_key) {
    std::string key = api_key.empty() ? GetApiKey() : api_key;
    if (url.empty()) {
        return "Error: Empty URL provided for web fetch.";
    }

    std::string endpoint = "https://api.fetch.tinyfish.ai";
    std::vector<std::string> headers = {
        "X-API-Key: " + key,
        "Content-Type: application/json",
        "Accept: application/json"
    };

    json payload = {
        {"urls", json::array({url})}
    };

    std::string response = HttpClient::Request("POST", endpoint, headers, payload.dump(), 25);
    if (response.empty()) {
        return "Error: Failed to connect to TinyFish Fetch API or empty response for URL: " + url;
    }

    try {
        json j = json::parse(response);
        if (j.contains("error")) {
            return "TinyFish Fetch API Error: " + j["error"].dump();
        }

        std::ostringstream out;
        if (j.contains("results") && j["results"].is_array() && !j["results"].empty()) {
            const auto& first = j["results"][0];
            std::string title = first.value("title", "Web Page");
            std::string final_url = first.value("final_url", url);
            std::string text_content = first.value("text", "");

            out << "### Web Page: [" << title << "](" << final_url << ")\n\n";
            if (!text_content.empty()) {
                // Truncate to reasonable length if excessively huge
                if (text_content.size() > 25000) {
                    out << text_content.substr(0, 25000) << "\n\n*(Content truncated at 25,000 characters)*\n";
                } else {
                    out << text_content << "\n";
                }
            } else {
                out << "(No readable text content extracted from page)\n";
            }
        } else {
            out << "No content returned from TinyFish Fetch for URL: " + url + "\n";
        }
        return out.str();
    } catch (const std::exception& e) {
        return "Error parsing TinyFish Fetch JSON response: " + std::string(e.what()) + "\nRaw response: " + response.substr(0, 500);
    }
}

std::string WebSearch::Execute(const std::string& search_query, const std::string& fetch_url, const std::string& api_key) {
    if (search_query.empty() && fetch_url.empty()) {
        return "Error: Neither 'search' query nor 'fetch' URL was provided for web_search.";
    }

    std::ostringstream combined;
    if (!search_query.empty()) {
        combined << Search(search_query, api_key);
    }
    if (!fetch_url.empty()) {
        if (!search_query.empty()) combined << "\n---\n\n";
        combined << Fetch(fetch_url, api_key);
    }
    return combined.str();
}

} // namespace razor
