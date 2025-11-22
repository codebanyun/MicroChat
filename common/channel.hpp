#pragma once
#include <brpc/channel.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include "spdlog.hpp"

namespace MicroChat {

class ServiceChannel {
public:
    using ChannelPtr = std::shared_ptr<brpc::Channel>;
    ServiceChannel(const std::string& name):service_name_(name), index_(0) {} 
    ~ServiceChannel() {}
    //服务上线新节点，即新增信道
    void add_channel(const std::string& host) 
    {
        brpc::ChannelOptions options;
        options.protocol = "baidu_std";
        options.timeout_ms = -1; // 不设置超时
        options.connect_timeout_ms = -1;
        options.max_retry = 3;
        ChannelPtr channel = std::make_shared<brpc::Channel>();
        if (channel->Init(host.c_str(), &options) == -1) {
            LOG_ERROR("初始化{}-{}信道失败!", service_name_, host);
            return;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        channels_.push_back(channel);
        hosts_[host] = channel;
        LOG_INFO("添加信道{}-{}成功", service_name_, host);
    }
    //服务下线节点，即删除信道
    void remove_channel(const std::string& host) 
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = hosts_.find(host);
        if (it != hosts_.end()) 
        {
            ChannelPtr channel = it->second;
            //从信道集合中移除
            for(auto vec_it = channels_.begin(); vec_it != channels_.end(); ++vec_it) 
            {
                if (*vec_it == channel) 
                {
                    channels_.erase(vec_it);
                    break;
                }
            }
            LOG_INFO("移除信道{}-{}成功", service_name_, host);
        } else {
            LOG_WARN("信道{}-{}不存在，移除失败", service_name_, host);
        }
        hosts_.erase(it);
    }
    //获取当前服务的信道，采用轮转方式获取
    ChannelPtr get_channel() 
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (channels_.empty()) {
            LOG_WARN("服务{}无可用信道", service_name_);
            return nullptr;
        }
        ChannelPtr channel = channels_[index_ % channels_.size()];
        index_ = (index_ + 1) % channels_.size();
        return channel;
    }
private:
    std::mutex mutex_;
    int32_t index_; //当前轮转下标计数器
    std::string service_name_;//服务名称
    std::vector<ChannelPtr> channels_; //当前服务对应的信道集合
    std::unordered_map<std::string, ChannelPtr> hosts_; //主机地址与信道映射关系
};

//总体的服务信道管理类
class ServiceManager {  
public:
    ServiceManager() {}
    ~ServiceManager() {}
    //添加关注的服务名称
    void add_service_name(const std::string& service_name) 
    {
        std::unique_lock<std::mutex> lock(mutex_);
        service_names_.insert(service_name);
    }
    //获取指定服务的信道节点
    ServiceChannel::ChannelPtr get_service_node(const std::string& service_name) 
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = services_.find(service_name);
        if (it != services_.end()) 
        {
            return it->second->get_channel();
        }
        return nullptr;
    }
    //服务上线的回调接口，添加新的信道节点
    void on_service_online(const std::string& service_instance, const std::string& host) 
    {
        std::string service_name = getServiceName(service_instance);
        std::shared_ptr<ServiceChannel> service;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = service_names_.find(service_name);
            if (it == service_names_.end()) {
                LOG_DEBUG("{}-{} 服务上线了，但当前并不关心！", service_name, host);
                return;
            }
            //先获取管理对象，没有则创建，有则添加节点
            auto sit = services_.find(service_name);
            if (sit == services_.end()) {
                service = std::make_shared<ServiceChannel>(service_name);
                services_.insert(std::make_pair(service_name, service));
            }else {
                service = sit->second;
            }
        }
        if (!service) {
            LOG_ERROR("新增 {} 服务管理节点失败！", service_name);
            return ;
        }
        service->add_channel(host);
        LOG_DEBUG("{}-{} 服务上线新节点，进行添加管理！", service_name, host);
    }
    //服务下线的回调接口，删除信道节点
    void on_service_offline(const std::string& service_instance, const std::string& host) 
    {
        std::string service_name = getServiceName(service_instance);
        std::shared_ptr<ServiceChannel> service;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = service_names_.find(service_name);
            if (it == service_names_.end()) {
                LOG_DEBUG("{}-{} 服务下线了，但当前并不关心！", service_name, host);
                return;
            }
            auto sit = services_.find(service_name);
            if (sit == services_.end()) {
                LOG_WARN("{} 服务管理节点不存在，无法移除下线节点 {}", service_name, host);
                return;
            }else {
                service = sit->second;
            }
        }
        if (!service) {
            LOG_ERROR("获取 {} 服务管理节点失败，无法移除下线节点 {}", service_name, host);
            return ;
        }
        service->remove_channel(host);
        LOG_DEBUG("{}-{} 服务下线节点，进行移除管理！", service_name, host);
    }
private:
    std::string getServiceName(const std::string& service_instance) 
    {
        size_t pos = service_instance.find_last_of('/');
        if (pos != std::string::npos) {
            return service_instance.substr(0, pos);
        }
        return service_instance;
    }
private:
    std::mutex mutex_;
    std::unordered_set<std::string> service_names_; //关注的服务名称集合
    std::unordered_map<std::string, std::shared_ptr<ServiceChannel>> services_; //服务名称与服务信道映射关系
};
} // namespace MicroChat