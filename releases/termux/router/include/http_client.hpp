#ifndef RAZOR_HTTP_CLIENT_HPP
#define RAZOR_HTTP_CLIENT_HPP

#include <string>
#include <vector>
#include <functional>

namespace razor {

class HttpClient {
public:
    // Performs a synchronous HTTP POST request with an optional timeout
    static std::string Post(const std::string& url, const std::string& api_key, const std::string& payload, long timeout_seconds = 0);

    // Performs a streaming HTTP POST request, calling on_chunk for each SSE data: line received.
    // Returns the raw HTTP response body accumulated from the transport stream.
    static std::string PostStream(
        const std::string& url,
        const std::string& api_key,
        const std::string& payload,
        std::function<void(const std::string& sse_data_line)> on_chunk,
        long timeout_seconds = 60
    );

    // Performs an HTTP request with custom method and headers
    static std::string Request(const std::string& method, const std::string& url, const std::vector<std::string>& headers, const std::string& payload = "", long timeout_seconds = 15);
};

} // namespace razor

#endif // RAZOR_HTTP_CLIENT_HPP
