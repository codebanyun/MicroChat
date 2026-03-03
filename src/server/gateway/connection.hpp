#pragma once
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include "spdlog.hpp"

namespace MicroChat {
    class Connection {
    public:
        struct Client {
            Client() = default;
            Client(const std::string &user_id, const std::string &session_id)
                : user_id(user_id), session_id(session_id) {}
            std::string user_id;
            std::string session_id;
        };
        typedef websocketpp::server<websocketpp::config::asio> server_t;
        Connection() = default;
        ~Connection() = default;
        void insert(const std::string &user_id, const std::string &session_id, const server_t::connection_ptr &conn) {
            std::lock_guard<std::mutex> lock(mtx_);
            user_connections_[user_id] = conn;
            connection_clients_[conn] = Client(user_id, session_id);
        }
        void remove(const server_t::connection_ptr &conn) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = connection_clients_.find(conn);
            if (it != connection_clients_.end()) {
                auto client = it->second;
                user_connections_.erase(it->second.user_id);
                connection_clients_.erase(it);
                LOG_INFO("长连接断开，user_id: {}, session_id: {}", client.user_id, client.session_id);
                return;
            }
            LOG_INFO("未找到长连接句柄: {}", reinterpret_cast<std::size_t>(conn.get()));
        }
        bool getClient(const server_t::connection_ptr &conn, std::string &session_id , std::string &user_id) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = connection_clients_.find(conn);
            if (it != connection_clients_.end()) {
                session_id = it->second.session_id;
                user_id = it->second.user_id;
                LOG_INFO("获取长连接信息成功，user_id: {}, session_id: {}", user_id, session_id);
                return true;
            }
            LOG_WARN("未找到长连接句柄: {}", reinterpret_cast<std::size_t>(conn.get()));
            return false;
        }
        server_t::connection_ptr getConnection(const std::string &user_id) {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = user_connections_.find(user_id);
            if (it != user_connections_.end()) {
                LOG_INFO("获取用户长连接成功，user_id: {}", user_id);
                return it->second;
            }
            LOG_WARN("未找到用户长连接，user_id: {}", user_id);
            return nullptr;
        }
        bool getClients(const std::unordered_set<std::string> &user_id_set, std::unordered_map<std::string, Client> &client_map) {
            std::lock_guard<std::mutex> lock(mtx_);
            for (const auto &user_id : user_id_set) {
                auto it = user_connections_.find(user_id);
                if (it != user_connections_.end()) {
                    auto conn_it = connection_clients_.find(it->second);
                    if (conn_it != connection_clients_.end()) {
                        client_map[user_id] = conn_it->second;
                    }
                }
            }
            LOG_INFO("获取批量用户长连接成功，user_id_count: {}", client_map.size());
            return !client_map.empty();
        }
    private:
        std::mutex mtx_;
        std::unordered_map<std::string, server_t::connection_ptr> user_connections_; //用户ID到连接句柄的映射
        std::unordered_map<server_t::connection_ptr, Client> connection_clients_; //连接句柄到客户端信息的映射
    };
}//namespace MicroChat