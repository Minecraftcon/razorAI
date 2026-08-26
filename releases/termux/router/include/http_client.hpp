#ifndef RAZOR_HTTP_CLIENT_HPP
#define RAZOR_HTTP_CLIENT_HPP

#include <string>
#include <vector>

namespace razor {

class HttpClient {
public:
    // Performs a synchronous HTTP POST request with an optional timeout
    static std::string Post(const std::string& url, const std::string& api_key, const std::string& payload, long timeout_seconds = 0);

    // Performs an HTTP request with custom method and headers
    static std::string Request(const std::string& method, const std::string& url, const std::vector<std::string>& headers, const std::string& payload = "", long timeout_seconds = 15);
};

} // namespace razor

#endif // RAZOR_HTTP_CLIENT_HPP
