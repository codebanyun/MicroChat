#pragma once
#include <sw/redis++/redis.h>
#include <string>
#include <memory>
#include <chrono>

namespace MicroChat{
    class RedisClientBuilder{
    public:
        static std::shared_ptr<sw::redis::Redis> build(const std::string &host = "127.0.0.1", int port = 6379,  int db = 0 , bool keep_alive = true){
            sw::redis::ConnectionOptions connection_options;
            connection_options.host = host;
            connection_options.port = port;
            connection_options.db = db;
            connection_options.keep_alive = keep_alive;
            return std::make_shared<sw::redis::Redis>(connection_options);
        }
    };
    class Session{
    public:
        Session(const std::shared_ptr<sw::redis::Redis> &redis_client):redis_client_(redis_client){}
        void set_session(const std::string &session_id, const std::string &user_id){
            redis_client_->set(session_id, user_id);
        }
        void delete_session(const std::string &session_id){
            redis_client_->del(session_id);
        }
        std::string get_uid(const std::string &session_id){
            //get返回sw::redis::OptionalString类型
            auto user_id = redis_client_->get(session_id);
            if(user_id){
                return *user_id;
            }
            return "";
        }
    private:
        std::shared_ptr<sw::redis::Redis> redis_client_;
    };
    class LoginStatus{
    public:
        LoginStatus(const std::shared_ptr<sw::redis::Redis> &redis_client):redis_client_(redis_client){}
        void adduser(const std::string &user_id){
            redis_client_->set(user_id, "");
        }
        void deleteuser(const std::string &user_id){
            redis_client_->del(user_id);
        }
        bool is_login(const std::string &user_id){
            auto val = redis_client_->get(user_id);
            return val.has_value();
        }
    private:
        std::shared_ptr<sw::redis::Redis> redis_client_;
    };
    class LoginToken{
    public:
        LoginToken(const std::shared_ptr<sw::redis::Redis> &redis_client):redis_client_(redis_client){}
        void set_token(const std::string &token_id, const std::string &token, int expire_ms = 60000){
            redis_client_->set(token_id, token, std::chrono::milliseconds(expire_ms));
        }
        void delete_token(const std::string &token_id){
            redis_client_->del(token_id);
        }
        std::string get_token(const std::string &token_id){
            auto token = redis_client_->get(token_id);
            if(token){
                return *token;
            }
            return "";
        }
    private:
        std::shared_ptr<sw::redis::Redis> redis_client_;
    };
} // namespace MicroChat
