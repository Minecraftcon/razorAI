#include "http_client.hpp"
#include "logger.hpp"
#include <curl/curl.h>
#include <iostream>
#include <chrono>

namespace razor {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), total_size);
    return total_size;
}

std::string HttpClient::Post(const std::string& url, const std::string& api_key, const std::string& payload, long timeout_seconds) {
    std::string response_string;
    int max_retries = 3;
    long timeout = timeout_seconds > 0 ? timeout_seconds : 60;

    RLOG_INFO("HttpClient::Post → " << url << " | timeout=" << timeout << "s | payload_len=" << payload.size());

    for (int attempt = 1; attempt <= max_retries; ++attempt) {
        response_string.clear();
        CURL* curl = curl_easy_init();
        if (!curl) {
            RLOG_ERROR("HttpClient::Post curl_easy_init() returned null on attempt " << attempt);
            continue;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        if (!api_key.empty()) {
            std::string auth_header = "Authorization: Bearer " + api_key;
            headers = curl_slist_append(headers, auth_header.c_str());
        }
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

        auto t0 = std::chrono::steady_clock::now();
        CURLcode res = curl_easy_perform(curl);
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t0).count();

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && http_code >= 200 && http_code < 500) {
            RLOG_INFO("HttpClient::Post OK | attempt=" << attempt
                      << " | HTTP=" << http_code << " | elapsed=" << elapsed_ms
                      << "ms | resp_len=" << response_string.size());
            return response_string;
        }

        RLOG_ERROR("HttpClient::Post FAILED | attempt=" << attempt
                   << "/" << max_retries
                   << " | url=" << url
                   << " | curl=" << curl_easy_strerror(res)
                   << " | HTTP=" << http_code
                   << " | elapsed=" << elapsed_ms << "ms");
        std::cerr << "[HttpClient] Attempt " << attempt << " to " << url 
                  << " failed (curl: " << curl_easy_strerror(res) 
                  << ", HTTP: " << http_code << "). "
                  << (attempt < max_retries ? "Retrying..." : "Exhausted retries.") << std::endl;
        
        if (attempt < max_retries) {
            timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 500000000; // 500ms
            nanosleep(&ts, nullptr);
        }
    }
    RLOG_ERROR("HttpClient::Post exhausted all retries for " << url);
    return response_string;
}

// ─── Streaming SSE support ───────────────────────────────────────────────────

struct StreamState {
    std::string buffer;
    std::string response_body;
    std::function<void(const std::string&)> on_chunk;
    size_t chunk_count = 0;
};

static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    StreamState* state = static_cast<StreamState*>(userp);
    state->buffer.append(static_cast<char*>(contents), total_size);
    state->response_body.append(static_cast<char*>(contents), total_size);

    size_t pos;
    while ((pos = state->buffer.find('\n')) != std::string::npos) {
        std::string line = state->buffer.substr(0, pos);
        state->buffer = state->buffer.substr(pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("data: ", 0) == 0) {
            std::string data = line.substr(6);
            if (data != "[DONE]" && !data.empty()) {
                state->chunk_count++;
                state->on_chunk(data);
            }
        }
    }
    return total_size;
}

std::string HttpClient::PostStream(
    const std::string& url,
    const std::string& api_key,
    const std::string& payload,
    std::function<void(const std::string&)> on_chunk,
    long timeout_seconds)
{
    RLOG_INFO("HttpClient::PostStream → " << url
              << " | timeout=" << (timeout_seconds > 0 ? timeout_seconds : 60L)
              << "s | payload_len=" << payload.size());

    StreamState state;
    state.on_chunk = std::move(on_chunk);

    CURL* curl = curl_easy_init();
    if (!curl) {
        RLOG_ERROR("HttpClient::PostStream curl_easy_init() returned null");
        return "";
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    // Abort only when the stream stalls, not when a long generation is still flowing.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, timeout_seconds > 0 ? timeout_seconds : 60L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!api_key.empty()) {
        std::string auth_header = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);

    auto t0 = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        RLOG_ERROR("HttpClient::PostStream curl FAILED | url=" << url
                   << " | curl=" << curl_easy_strerror(res)
                   << " | HTTP=" << http_code
                   << " | elapsed=" << elapsed_ms << "ms"
                   << " | chunks_received=" << state.chunk_count);
        constexpr size_t kMaxLoggedBody = 2048;
        if (!state.response_body.empty()) {
            RLOG_ERROR("HttpClient::PostStream raw body (curl fail) | url=" << url
                       << " | body_len=" << state.response_body.size()
                       << " | body=\n" << state.response_body.substr(0, kMaxLoggedBody));
        }
        std::cerr << "[HttpClient::PostStream] curl failed: " << curl_easy_strerror(res) << std::endl;
    } else {
        RLOG_INFO("HttpClient::PostStream done | url=" << url
                  << " | HTTP=" << http_code
                  << " | elapsed=" << elapsed_ms << "ms"
                  << " | chunks_received=" << state.chunk_count);
        constexpr size_t kMaxLoggedBody = 2048;
        if (http_code >= 400 || state.chunk_count == 0) {
            RLOG_INFO("HttpClient::PostStream raw body | url=" << url
                      << " | HTTP=" << http_code
                      << " | body_len=" << state.response_body.size()
                      << " | body=\n" << state.response_body.substr(0, kMaxLoggedBody));
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return state.response_body;
}

std::string HttpClient::Request(const std::string& method, const std::string& url, const std::vector<std::string>& headers, const std::string& payload, long timeout_seconds) {
    RLOG_INFO("HttpClient::Request " << method << " → " << url
              << " | timeout=" << timeout_seconds << "s | payload_len=" << payload.size());

    std::string response_string;
    CURL* curl = curl_easy_init();
    if (!curl) {
        RLOG_ERROR("HttpClient::Request curl_easy_init() returned null | url=" << url);
        return "";
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    struct curl_slist* chunk = NULL;
    for (const auto& h : headers) {
        chunk = curl_slist_append(chunk, h.c_str());
    }
    if (chunk) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }
    
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!payload.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        }
    } else if (method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!payload.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        }
    }
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    if (timeout_seconds > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    }

    auto t0 = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (res != CURLE_OK) {
        RLOG_ERROR("HttpClient::Request FAILED | method=" << method
                   << " | url=" << url
                   << " | curl=" << curl_easy_strerror(res)
                   << " | HTTP=" << http_code
                   << " | elapsed=" << elapsed_ms << "ms");
        std::cerr << "curl_easy_perform(" << url << ") failed: " << curl_easy_strerror(res) << std::endl;
        response_string = "";
    } else {
        RLOG_INFO("HttpClient::Request OK | method=" << method
                  << " | HTTP=" << http_code
                  << " | elapsed=" << elapsed_ms << "ms"
                  << " | resp_len=" << response_string.size());
    }
    
    if (chunk) {
        curl_slist_free_all(chunk);
    }
    curl_easy_cleanup(curl);
    return response_string;
}

} // namespace razor
