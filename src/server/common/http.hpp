#pragma once

#include "server.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace httplib {

using Headers = std::unordered_multimap<std::string, std::string>;

struct Request {
    std::string method;
    std::string path;
    std::string version = "HTTP/1.1";
    std::string body;
    std::smatch matches;
    Headers headers;
    std::unordered_map<std::string, std::string> params;

    void clear() {
        method.clear();
        path.clear();
        version = "HTTP/1.1";
        body.clear();
        std::smatch tmp;
        matches.swap(tmp);
        headers.clear();
        params.clear();
    }

    void set_header(const std::string &key, const std::string &val) {
        auto normalized = normalize_header_key(key);
        headers.erase(normalized);
        headers.emplace(std::move(normalized), val);
    }

    bool has_header(const std::string &key) const {
        auto normalized = normalize_header_key(key);
        return headers.find(normalized) != headers.end();
    }

    std::string get_header_value(const std::string &key, const char *def = "") const {
        auto normalized = normalize_header_key(key);
        auto it = headers.find(normalized);
        return it == headers.end() ? std::string(def) : it->second;
    }

    void set_param(const std::string &key, const std::string &val) {
        params[key] = val;
    }

    bool has_param(const std::string &key) const {
        return params.find(key) != params.end();
    }

    std::string get_param_value(const std::string &key, const char *def = "") const {
        auto it = params.find(key);
        return it == params.end() ? std::string(def) : it->second;
    }

    bool try_content_length(size_t &len) const {
        if (!has_header("Content-Length")) {
            len = 0;
            return true;
        }
        const auto raw = get_header_value("Content-Length");
        if (raw.empty()) {
            len = 0;
            return false;
        }
        try {
            size_t pos = 0;
            auto parsed = std::stoull(raw, &pos);
            if (pos != raw.size()) {
                return false;
            }
            len = static_cast<size_t>(parsed);
            return true;
        } catch (...) {
            len = 0;
            return false;
        }
    }

    bool is_keep_alive() const {
        auto connection = to_lower_copy(get_header_value("Connection"));
        if (version == "HTTP/1.0") {
            return connection == "keep-alive";
        }
        return connection != "close";
    }

private:
    static std::string normalize_header_key(const std::string &key) {
        return to_lower_copy(key);
    }

    static std::string to_lower_copy(const std::string &input) {
        std::string output = input;
        std::transform(output.begin(), output.end(), output.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return output;
    }

    friend class Server;
    friend class HttpContext;
};

struct Response {
    std::string version = "HTTP/1.1";
    int status = 200;
    std::string body;
    Headers headers;

    void set_header(const std::string &key, const std::string &val) {
        auto normalized = normalize_header_key(key);
        headers.erase(normalized);
        headers.emplace(std::move(normalized), val);
    }

    bool has_header(const std::string &key) const {
        auto normalized = normalize_header_key(key);
        return headers.find(normalized) != headers.end();
    }

    std::string get_header_value(const std::string &key, const char *def = "") const {
        auto normalized = normalize_header_key(key);
        auto it = headers.find(normalized);
        return it == headers.end() ? std::string(def) : it->second;
    }

    void set_content(const std::string &content, const std::string &content_type) {
        body = content;
        set_header("Content-Type", content_type);
    }

    void set_content(std::string &&content, const std::string &content_type) {
        body = std::move(content);
        set_header("Content-Type", content_type);
    }

    void set_redirect(const std::string &url, int status_code = 302) {
        status = status_code;
        set_header("Location", url);
    }

private:
    static std::string normalize_header_key(const std::string &key) {
        std::string output = key;
        std::transform(output.begin(), output.end(), output.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return output;
    }

    friend class Server;
};

class HttpContext {
public:
    enum class State {
        Error,
        RequestLine,
        Headers,
        Body,
        Over,
    };

    HttpContext() : response_status_(200), state_(State::RequestLine) {}

    void reset() {
        response_status_ = 200;
        state_ = State::RequestLine;
        request_.clear();
    }

    int response_status() const { return response_status_; }
    State state() const { return state_; }
    Request &request() { return request_; }

    void recv_http_request(Buffer *buffer) {
        switch (state_) {
            case State::RequestLine:
                if (!recv_request_line(buffer)) return;
                [[fallthrough]];
            case State::Headers:
                if (!recv_headers(buffer)) return;
                [[fallthrough]];
            case State::Body:
                recv_body(buffer);
                break;
            case State::Over:
            case State::Error:
                break;
        }
    }

private:
    static constexpr size_t kMaxLine = 8192;

    int response_status_;
    State state_;
    Request request_;

    static std::string url_decode(const std::string &value, bool plus_to_space) {
        auto hex_to_int = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };

        std::string result;
        result.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i) {
            if (plus_to_space && value[i] == '+') {
                result.push_back(' ');
                continue;
            }
            if (value[i] == '%' && i + 2 < value.size()) {
                int hi = hex_to_int(value[i + 1]);
                int lo = hex_to_int(value[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    result.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            result.push_back(value[i]);
        }
        return result;
    }

    static void split_query(const std::string &query, Request &request) {
        size_t start = 0;
        while (start < query.size()) {
            size_t amp = query.find('&', start);
            std::string item = amp == std::string::npos ? query.substr(start) : query.substr(start, amp - start);
            if (!item.empty()) {
                size_t eq = item.find('=');
                if (eq != std::string::npos) {
                    request.set_param(url_decode(item.substr(0, eq), true),
                                      url_decode(item.substr(eq + 1), true));
                }
            }
            if (amp == std::string::npos) break;
            start = amp + 1;
        }
    }

    bool parse_request_line(std::string line) {
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty() && line.back() == '\r') line.pop_back();

        static const std::regex pattern(
            R"((OPTIONS|POST|GET|HEAD|PUT|DELETE) ([^? ]*)(?:\?(.*))? (HTTP/1\.[01]))",
            std::regex::icase);
        std::smatch matches;
        if (!std::regex_match(line, matches, pattern)) {
            state_ = State::Error;
            response_status_ = 400;
            return false;
        }

        request_.method = matches[1].str();
        std::transform(request_.method.begin(), request_.method.end(), request_.method.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        request_.path = url_decode(matches[2].str(), false);
        request_.version = matches[4].str();
        if (matches.size() > 3) {
            split_query(matches[3].str(), request_);
        }
        return true;
    }

    bool recv_request_line(Buffer *buffer) {
        if (state_ != State::RequestLine) return false;
        std::string line = buffer->GetLine();
        if (line.empty()) {
            if (buffer->ReadableSize() > kMaxLine) {
                state_ = State::Error;
                response_status_ = 414;
            }
            return false;
        }
        if (line.size() > kMaxLine || !parse_request_line(std::move(line))) {
            if (response_status_ == 200) response_status_ = 414;
            return false;
        }
        state_ = State::Headers;
        return true;
    }

    bool parse_header(std::string line) {
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto pos = line.find(':');
        if (pos == std::string::npos) {
            state_ = State::Error;
            response_status_ = 400;
            return false;
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        request_.set_header(key, value);
        return true;
    }

    bool recv_headers(Buffer *buffer) {
        if (state_ != State::Headers) return false;
        while (true) {
            std::string line = buffer->GetLine();
            if (line.empty()) {
                if (buffer->ReadableSize() > kMaxLine) {
                    state_ = State::Error;
                    response_status_ = 431;
                }
                return false;
            }
            if (line.size() > kMaxLine) {
                state_ = State::Error;
                response_status_ = 431;
                return false;
            }
            if (line == "\n" || line == "\r\n") {
                size_t length = 0;
                if (!request_.try_content_length(length)) {
                    state_ = State::Error;
                    response_status_ = 400;
                    return false;
                }
                state_ = State::Body;
                return true;
            }
            if (!parse_header(std::move(line))) {
                return false;
            }
        }
    }

    void recv_body(Buffer *buffer) {
        if (state_ != State::Body) return;
        size_t content_length = 0;
        if (!request_.try_content_length(content_length)) {
            state_ = State::Error;
            response_status_ = 400;
            return;
        }
        if (content_length == 0) {
            state_ = State::Over;
            return;
        }
        if (request_.body.size() >= content_length) {
            state_ = State::Over;
            return;
        }
        size_t need = content_length - request_.body.size();
        size_t readable = buffer->ReadableSize();
        size_t take = std::min(need, readable);
        if (take > 0) {
            request_.body.append(buffer->CurReadPos(), take);
            buffer->MoveReadOffset(take);
        }
        if (request_.body.size() == content_length) {
            state_ = State::Over;
        }
    }
};

class Server {
public:
    using Handler = std::function<void(const Request &, Response &)>;
    enum class HandlerResponse {
        Unhandled,
        Handled,
    };
    using PreRoutingHandler = std::function<HandlerResponse(const Request &, Response &)>;

    Server()
        : timeout_seconds_(20),
          thread_count_(default_thread_count()) {}

    void set_default_headers(std::initializer_list<std::pair<std::string, std::string>> headers) {
        default_headers_.clear();
        for (const auto &header : headers) {
            auto key = normalize_header_key(header.first);
            default_headers_.erase(key);
            default_headers_.emplace(std::move(key), header.second);
        }
    }

    void set_pre_routing_handler(const PreRoutingHandler &handler) {
        pre_routing_handler_ = handler;
    }

    void Post(const std::string &pattern, const Handler &handler) {
        post_routes_[pattern] = handler;
    }

    void set_idle_timeout(int seconds) {
        if (seconds > 0) {
            timeout_seconds_ = seconds;
        }
    }

    void set_thread_count(int count) {
        if (count > 0) {
            thread_count_ = count;
        }
    }

    bool listen(const std::string &host, int port) {
        if (port <= 0) {
            throw std::invalid_argument("invalid http port");
        }
        if (!host.empty() && host != "0.0.0.0") {
            INF_LOG("custom host %s is ignored, listen on 0.0.0.0", host.c_str());
        }

        server_ = std::make_unique<TcpServer>(port);
        server_->SetThreadNum(thread_count_);
        server_->EnableInactiveRelease(timeout_seconds_);
        server_->SetConnectedCallback(std::bind(&Server::on_connected, this, std::placeholders::_1));
        server_->SetMessageCallback(std::bind(&Server::on_message, this, std::placeholders::_1, std::placeholders::_2));
        server_->Start();
        return true;
    }

private:
    int timeout_seconds_;
    int thread_count_;
    std::unordered_map<std::string, Handler> post_routes_;
    Headers default_headers_;
    PreRoutingHandler pre_routing_handler_;
    std::unique_ptr<TcpServer> server_;

    static int default_thread_count() {
        auto count = std::thread::hardware_concurrency();
        if (count == 0) return 4;
        return static_cast<int>(count);
    }

    static std::string normalize_header_key(const std::string &key) {
        std::string output = key;
        std::transform(output.begin(), output.end(), output.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return output;
    }

    static const char *status_message(int status) {
        static const std::unordered_map<int, const char *> status_table = {
            {200, "OK"},
            {204, "No Content"},
            {400, "Bad Request"},
            {404, "Not Found"},
            {405, "Method Not Allowed"},
            {413, "Payload Too Large"},
            {414, "URI Too Long"},
            {415, "Unsupported Media Type"},
            {431, "Request Header Fields Too Large"},
            {500, "Internal Server Error"},
            {502, "Bad Gateway"},
            {503, "Service Unavailable"},
            {504, "Gateway Timeout"},
        };
        auto it = status_table.find(status);
        return it == status_table.end() ? "Unknown" : it->second;
    }

    void on_connected(const PtrConnection &conn) {
        conn->SetContext(HttpContext());
    }

    void apply_default_headers(Response &response) {
        for (const auto &header : default_headers_) {
            if (!response.has_header(header.first)) {
                response.headers.emplace(header.first, header.second);
            }
        }
    }

    void write_response(const PtrConnection &conn, const Request &request, Response &response) {
        apply_default_headers(response);

        bool keep_alive = request.is_keep_alive();
        response.set_header("Connection", keep_alive ? "keep-alive" : "close");
        if (!response.has_header("Content-Length")) {
            response.set_header("Content-Length", std::to_string(response.body.size()));
        }
        if (!response.body.empty() && !response.has_header("Content-Type")) {
            response.set_header("Content-Type", "application/octet-stream");
        }

        std::ostringstream stream;
        stream << request.version << ' ' << response.status << ' ' << status_message(response.status) << "\r\n";
        for (const auto &header : response.headers) {
            stream << header.first << ": " << header.second << "\r\n";
        }
        stream << "\r\n";
        stream << response.body;

        auto payload = stream.str();
        conn->Send(payload.data(), payload.size());
    }

    void route_request(const Request &request, Response &response) {
        if (request.method == "POST") {
            auto it = post_routes_.find(request.path);
            if (it != post_routes_.end()) {
                it->second(request, response);
                return;
            }
            response.status = 404;
            response.set_content("not found", "text/plain");
            return;
        }

        response.status = 405;
        response.set_content("method not allowed", "text/plain");
    }

    void handle_parse_error(const PtrConnection &conn, Buffer *buffer, HttpContext *context) {
        Response response;
        response.status = context->response_status();
        response.set_content("bad request", "text/plain");
        write_response(conn, context->request(), response);
        context->reset();
        buffer->MoveReadOffset(buffer->ReadableSize());
        conn->Shutdown();
    }

    void on_message(const PtrConnection &conn, Buffer *buffer) {
        while (buffer->ReadableSize() > 0) {
            auto *context = conn->GetContext()->get<HttpContext>();
            context->recv_http_request(buffer);
            if (context->response_status() >= 400) {
                handle_parse_error(conn, buffer, context);
                return;
            }
            if (context->state() != HttpContext::State::Over) {
                return;
            }

            Request &request = context->request();
            Response response;
            bool handled = false;
            if (pre_routing_handler_) {
                handled = pre_routing_handler_(request, response) == HandlerResponse::Handled;
            }
            if (!handled) {
                route_request(request, response);
            }
            write_response(conn, request, response);
            bool keep_alive = request.is_keep_alive();
            context->reset();
            if (!keep_alive) {
                conn->Shutdown();
                return;
            }
        }
    }
};

} // namespace httplib
