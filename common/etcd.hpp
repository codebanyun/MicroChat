#pragma once
#include <etcd/Client.hpp>
#include <etcd/Response.hpp>
#include <etcd/Watcher.hpp>
#include <etcd/KeepAlive.hpp>
#include <string>
#include <memory>
#include <functional>
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
    ~EtcdClientRegistry() {
        unregister_service();
    }
    bool register_service(const std::string& key, const std::string& value, int ttl = 3)
    {
        // 创建租约并保持存活shared_ptr
        auto lease_resp = client_->leasekeepalive(ttl).get();
        if (!lease_resp) {
            LOG_ERROR("Failed to keep alive lease");
            return false;
        }
        lease_id_ = lease_resp->Lease();
        keep_alive_ = lease_resp;
        auto put_resp = client_->put(key, value, lease_id_).get();
        if(put_resp.is_ok() == false) {
            LOG_ERROR("Failed to register service : {}", put_resp.error_message());
            return false;
        }
       return true;
    }
    void unregister_service() {
        if (keep_alive_) {
            try { keep_alive_->Cancel(); } catch(...) {}
            keep_alive_.reset();
        }
        // 可选：如果想显式撤销租约，可调用 client_->revoke(lease_id_).get();
    }
private:
    std::shared_ptr<etcd::Client> client_;
    std::shared_ptr<etcd::KeepAlive> keep_alive_ = nullptr;
    uint64_t lease_id_;
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
                LOG_DEBUG("下线服务：{}-{}", ev.prev_kv().key(), ev.prev_kv().as_string());
                if (delete_callback_) delete_callback_(ev.prev_kv().key(), ev.prev_kv().as_string());
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