#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>

namespace razor {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), total_size);
    return total_size;
}

std::string HttpClient::Post(const std::string& url, const std::string& api_key, const std::string& payload, long timeout_seconds) {
    std::string response_string;
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        
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
        
        if (timeout_seconds > 0) {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
        }
        
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            response_string = "";
        }
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response_string;
}

std::string HttpClient::Request(const std::string& method, const std::string& url, const std::vector<std::string>& headers, const std::string& payload, long timeout_seconds) {
    std::string response_string;
    CURL* curl = curl_easy_init();
    if (curl) {
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
        
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform(" << url << ") failed: " << curl_easy_strerror(res) << std::endl;
            response_string = "";
        }
        
        if (chunk) {
            curl_slist_free_all(chunk);
        }
        curl_easy_cleanup(curl);
    }
    return response_string;
}

} // namespace razor
