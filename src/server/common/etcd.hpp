#pragma once
#include <etcd/Client.hpp>
#include <etcd/Response.hpp>
#include <etcd/Watcher.hpp>
#include <etcd/KeepAlive.hpp>
#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <chrono>
#include "spdlog.hpp"

namespace MicroChat {
/*
*@brief: Etcd客户端封装类，提供服务注册功能
*@param: etcd_host etcd服务器地址
*@param: key 服务注册的键
*@param: value 服务注册的值
*@param: ttl 租约时间，单位秒
*@return: true 注册成功，false 注册失败
*/
class EtcdClientRegistry {
public:
    EtcdClientRegistry(const std::string& etcd_host)
        : client_(std::make_shared<etcd::Client>(etcd_host)) {}  
    EtcdClientRegistry(const EtcdClientRegistry&) = delete;
    EtcdClientRegistry& operator=(const EtcdClientRegistry&) = delete;
    EtcdClientRegistry(EtcdClientRegistry&&) = default;
    EtcdClientRegistry& operator=(EtcdClientRegistry&&) = default;
    ~EtcdClientRegistry() {
        unregister_service();
    }
    bool register_service(const std::string& key, const std::string& value, int ttl = 30)
    {
        constexpr int kMaxAttempts = 15;
        constexpr auto kRetryDelay = std::chrono::seconds(1);

        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            try {
                auto lease_resp = client_->leasekeepalive(ttl).get();
                if (!lease_resp) {
                    LOG_WARN("Failed to keep alive lease, attempt {}/{}", attempt, kMaxAttempts);
                } else {
                    auto lease_id = lease_resp->Lease();
                    auto put_resp = client_->put(key, value, lease_id).get();
                    if (put_resp.is_ok()) {
                        lease_id_ = lease_id;
                        keep_alive_ = lease_resp;
                        LOG_INFO("Service registered to etcd successfully: {} -> {}", key, value);
                        return true;
                    }
                    try { lease_resp->Cancel(); } catch (...) {}
                    LOG_WARN("Failed to register service, attempt {}/{}: {}", attempt, kMaxAttempts, put_resp.error_message());
                }
            } catch (const std::exception& e) {
                LOG_WARN("Exception while registering service, attempt {}/{}: {}", attempt, kMaxAttempts, e.what());
            }

            std::this_thread::sleep_for(kRetryDelay);
        }

        LOG_ERROR("Failed to register service after retries: {} -> {}", key, value);
        return false;
    }
    void unregister_service() {
        if (keep_alive_) {
            LOG_INFO("Unregister service lease from etcd: {}", lease_id_);
            try { keep_alive_->Cancel(); } catch(...) {}
            keep_alive_.reset();
            lease_id_ = 0;
        }
    }
private:
    std::shared_ptr<etcd::Client> client_;
    std::shared_ptr<etcd::KeepAlive> keep_alive_ = nullptr;
    uint64_t lease_id_ = 0;
};


/*
*@brief: Etcd客户端封装类，提供服务发现功能
*@param: etcd_host etcd服务器地址
*@param: base_dir 服务注册的基础目录
*@param: putcallback 服务上线回调函数
*@param: deletecallback 服务下线回调函数
*/
class EtcdClientfinder {
public:
    using NotifyCallback = std::function<void(std::string, std::string)>;  
    EtcdClientfinder(const EtcdClientfinder&) = delete;
    EtcdClientfinder& operator=(const EtcdClientfinder&) = delete;
    EtcdClientfinder(EtcdClientfinder&&) = default;
    EtcdClientfinder& operator=(EtcdClientfinder&&) = default;
    ~EtcdClientfinder() {  
        stop();
    }
    EtcdClientfinder(const std::string& host,
                    const std::string& base_dir = "/service/",
                    const NotifyCallback& putcallback = nullptr,
                    const NotifyCallback& deletecallback = nullptr)
        : client_(std::make_shared<etcd::Client>(host)),
            put_callback_(putcallback),
            delete_callback_(deletecallback)    
    {
        //先进行服务发现,先获取到当前已有的数据
        auto resp = client_->ls(base_dir).get();
        if (resp.is_ok() == false) {    
            LOG_ERROR("Failed to get initial service list : {}", resp.error_message());
            return;
        }
        int sz = resp.keys().size();
        for (int i = 0; i < sz; i++) {
            if (put_callback_) {   
                put_callback_(resp.key(i), resp.value(i).as_string());
            }
        }
        //然后启动监听器，监听后续的变化
        //true为递归监听
        watcher_ = std::make_shared<etcd::Watcher>(
            *client_, base_dir, 
            [this](const etcd::Response& resp){callback(resp);},
            true);
    }
    void stop() {
        if (watcher_) {
            try { watcher_->Cancel(); } catch(...) {}
            watcher_.reset();
        }
    }
private:
    void callback(const etcd::Response &resp) {
        if (resp.is_ok() == false) {
            LOG_ERROR("Received error event notification: {}", resp.error_message());
            return;
        }
        for (auto const& ev : resp.events()) {
            if (ev.event_type() == etcd::Event::EventType::PUT) {
                LOG_INFO("服务上线：{}-{}", ev.kv().key(), ev.kv().as_string());
                if (put_callback_) {
                    put_callback_(ev.kv().key(), ev.kv().as_string());
                }
            } else if (ev.event_type() == etcd::Event::EventType::DELETE_) {
                if (delete_callback_) delete_callback_(ev.prev_kv().key(), ev.prev_kv().as_string());
                LOG_DEBUG("下线服务：{}-{}", ev.prev_kv().key(), ev.prev_kv().as_string());
            }
        }
    }
private:
    std::shared_ptr<etcd::Client> client_;
    std::shared_ptr<etcd::Watcher> watcher_ = nullptr;
    NotifyCallback put_callback_;
    NotifyCallback delete_callback_;
};

} // namespace MicroChat