#pragma once
#include <ev.h>
#include <amqpcpp.h>
#include <amqpcpp/libev.h>
#include <openssl/ssl.h>
#include <openssl/opensslv.h>
#include <iostream>
#include <functional>
#include "spdlog.hpp"

namespace MicroChat {

    class RabbitMQClient {
        public:
            using MessageCallback = std::function<void(const char*, size_t)>;
            RabbitMQClient(const std::string &user, 
                        const std::string &passwd,
                        const std::string &host) {
                //1. 实例化底层网络通信框架的I/O事件监控句柄
                loop_ = EV_DEFAULT;
                //2. 实例化libEvHandler句柄
                handler_ = std::make_unique<AMQP::LibEvHandler>(loop_);
                //3. 实例化连接参数
                std::string address_str = "amqp://" + user + ":" + passwd + "@" + host + "/";
                AMQP::Address address(address_str);
                connection_ = std::make_unique<AMQP::TcpConnection>(handler_.get(), address);
                //4. 实例化信道对象
                channel_ =  std::make_unique<AMQP::TcpChannel>(connection_.get());
                //启动事件循环线程
                loop_thread_ = std::thread([this]() {
                    ev_run(loop_, 0);
                });
            }            
            ~RabbitMQClient() {
                ev_async_init(&async_watcher_, watcher_callback);
                ev_async_start(loop_, &async_watcher_);
                ev_async_send(loop_, &async_watcher_);
                if (loop_thread_.joinable()) {
                    loop_thread_.join();
                }
                loop_ = nullptr;
            }
            void declareComponents(const std::string &exchange,
                                    const std::string &queue,
                                    const std::string &routing_key = "routing_key",
                                    AMQP::ExchangeType exchange_type = AMQP::ExchangeType::direct){
                channel_->declareExchange(exchange, exchange_type)
                    .onError([](const char *message) {
                        LOG_ERROR("声明交换机失败：{}", message);
                        exit(0);
                    })
                    .onSuccess([exchange](){
                        LOG_INFO("{} 交换机创建成功！", exchange);
                    });
                channel_->declareQueue(queue, AMQP::durable)
                    .onError([](const char *message) {
                        LOG_ERROR("声明队列失败：{}", message);
                        exit(0);
                    })
                    .onSuccess([queue](){
                        LOG_INFO("{} 队列创建成功！", queue);
                    });
                channel_->bindQueue(exchange, queue, routing_key)
                    .onError([](const char *message) {
                        LOG_ERROR("绑定交换机和队列失败：{}", message);
                        exit(0);
                    })
                    .onSuccess([exchange, queue , routing_key](){
                        LOG_INFO("绑定交换机和队列成功 {}-{}-{}！", exchange, queue, routing_key);
                    });
            }
            bool publichMessage(const std::string &exchange,
                                const std::string &routing_key,
                                const std::string &message){
                LOG_DEBUG("向交换机 {} 通过路由键 {} 发布消息：{}", exchange, routing_key, message);
                bool ret =  channel_->publish(exchange, routing_key, message);
                if(!ret) {
                    LOG_ERROR("向交换机 {} 通过路由键 {} 发布消息失败：{}", exchange, routing_key, message);
                }
                return ret;
            }
            void consumeMessage(const std::string &queue,
                                const MessageCallback &message_cb){
                LOG_DEBUG("开始订阅 {} 队列消息！", queue);
                auto callback = [this, message_cb](const AMQP::Message &message, 
                    uint64_t deliveryTag, 
                    bool redelivered) {
                    message_cb(message.body(), message.bodySize());
                    channel_->ack(deliveryTag);
                };
                channel_->consume(queue, "consume-tag")  //返回值 DeferredConsumer
                    .onReceived(callback)
                    .onError([queue](const char *message){
                        LOG_ERROR("订阅 {} 队列消息失败: {}", queue, message);
                        exit(0);
                    }); // 返回值是 AMQP::Deferred
            }
        private:
            static void watcher_callback(struct ev_loop *loop, ev_async *watcher, int32_t revents) {
                ev_break(loop, EVBREAK_ALL);
            }
        private:
            struct ev_async async_watcher_;
            struct ev_loop *loop_;
            std::unique_ptr<AMQP::LibEvHandler> handler_;
            std::unique_ptr<AMQP::TcpConnection> connection_;
            std::unique_ptr<AMQP::TcpChannel> channel_;
            std::thread loop_thread_;
    };

}