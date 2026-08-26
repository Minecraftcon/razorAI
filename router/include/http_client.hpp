#ifndef RAZOR_HTTP_CLIENT_HPP
#define RAZOR_HTTP_CLIENT_HPP

#include <string>

namespace razor {

class HttpClient {
public:
    // Performs a synchronous HTTP POST request with an optional timeout
    static std::string Post(const std::string& url, const std::string& api_key, const std::string& payload, long timeout_seconds = 0);
};

} // namespace razor

#endif // RAZOR_HTTP_CLIENT_HPP
