#pragma once
#include <ev.h>
#include <amqpcpp.h>
#include <amqpcpp/libev.h>
#include <openssl/ssl.h>
#include <openssl/opensslv.h>
#include <iostream>
#include <functional>
#include <cstdint>
#include "spdlog.hpp"

namespace MicroChat {

    class RabbitMQClient {
        public:
            using MessageCallback = std::function<bool(const char*, size_t)>;
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
            void declareComponentsWithRetry(const std::string &main_exchange,
                                            const std::string &main_queue,
                                            const std::string &main_routing_key,
                                            const std::string &retry_exchange,
                                            const std::string &retry_queue,
                                            const std::string &retry_routing_key,
                                            const std::string &dead_exchange,
                                            const std::string &dead_queue,
                                            const std::string &dead_routing_key,
                                            uint32_t retry_delay_ms = 10000,
                                            uint32_t max_retry_count = 3,
                                            AMQP::ExchangeType exchange_type = AMQP::ExchangeType::direct) {
                declareComponents(main_exchange, main_queue, main_routing_key, exchange_type);

                channel_->declareExchange(retry_exchange, exchange_type)
                    .onError([](const char *message) {
                        LOG_ERROR("声明重试交换机失败：{}", message);
                        exit(0);
                    });

                AMQP::Table retry_args;
                retry_args.set("x-dead-letter-exchange", main_exchange);
                retry_args.set("x-dead-letter-routing-key", main_routing_key);
                retry_args.set("x-message-ttl", retry_delay_ms);
                channel_->declareQueue(retry_queue, AMQP::durable, retry_args)
                    .onError([](const char *message) {
                        LOG_ERROR("声明重试队列失败：{}", message);
                        exit(0);
                    });
                channel_->bindQueue(retry_exchange, retry_queue, retry_routing_key)
                    .onError([](const char *message) {
                        LOG_ERROR("绑定重试交换机和队列失败：{}", message);
                        exit(0);
                    });

                channel_->declareExchange(dead_exchange, exchange_type)
                    .onError([](const char *message) {
                        LOG_ERROR("声明死信交换机失败：{}", message);
                        exit(0);
                    });
                channel_->declareQueue(dead_queue, AMQP::durable)
                    .onError([](const char *message) {
                        LOG_ERROR("声明死信队列失败：{}", message);
                        exit(0);
                    });
                channel_->bindQueue(dead_exchange, dead_queue, dead_routing_key)
                    .onError([](const char *message) {
                        LOG_ERROR("绑定死信交换机和队列失败：{}", message);
                        exit(0);
                    });

                retry_exchange_name_ = retry_exchange;
                retry_routing_key_ = retry_routing_key;
                dead_exchange_name_ = dead_exchange;
                dead_routing_key_ = dead_routing_key;
                retry_delay_ms_ = retry_delay_ms;
                max_retry_count_ = max_retry_count;
                retry_mode_enabled_ = true;
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
                    bool ok = message_cb(message.body(), message.bodySize());
                    if (ok) {
                        channel_->ack(deliveryTag);
                        return;
                    }
                    if (retry_mode_enabled_) {
                        uint32_t retry_count = extractRetryCount(message);
                        uint32_t next_retry_count = retry_count + 1;
                        bool publish_ok = false;
                        if (next_retry_count >= max_retry_count_) {
                            publish_ok = publishWithRetryHeader(dead_exchange_name_,
                                                                dead_routing_key_,
                                                                message.body(),
                                                                message.bodySize(),
                                                                next_retry_count,
                                                                0);
                            if (!publish_ok) {
                                LOG_ERROR("投递到死信队列失败，delivery_tag: {}", deliveryTag);
                            }
                        } else {
                            publish_ok = publishWithRetryHeader(retry_exchange_name_,
                                                                retry_routing_key_,
                                                                message.body(),
                                                                message.bodySize(),
                                                                next_retry_count,
                                                                retry_delay_ms_);
                            if (!publish_ok) {
                                LOG_ERROR("投递到重试队列失败，delivery_tag: {}, retry_count: {}", deliveryTag, next_retry_count);
                            }
                        }
                        if (publish_ok) {
                            channel_->ack(deliveryTag);
                        } else {
                            channel_->reject(deliveryTag, true);
                        }
                        return;
                    }
                    if (redelivered) {
                        channel_->reject(deliveryTag, false);
                    } else {
                        channel_->reject(deliveryTag, true);
                    }
                };
                channel_->consume(queue, "consume-tag")  //返回值 DeferredConsumer
                    .onReceived(callback)
                    .onError([queue](const char *message){
                        LOG_ERROR("订阅 {} 队列消息失败: {}", queue, message);
                        exit(0);
                    }); // 返回值是 AMQP::Deferred
            }
        private:
            uint32_t extractRetryCount(const AMQP::Message &message) {
                if (!message.hasHeaders()) {
                    return 0;
                }
                const AMQP::Table &headers = message.headers();
                if (!headers.contains("x-retry-count")) {
                    return 0;
                }
                try {
                    return static_cast<uint32_t>(headers["x-retry-count"]);
                } catch (...) {
                    return 0;
                }
            }
            bool publishWithRetryHeader(const std::string &exchange,
                                        const std::string &routing_key,
                                        const char *body,
                                        size_t body_size,
                                        uint32_t retry_count,
                                        uint32_t expiration_ms) {
                AMQP::Envelope envelope(body, body_size);
                envelope.setPersistent(true);
                AMQP::Table headers;
                headers.set("x-retry-count", retry_count);
                envelope.setHeaders(std::move(headers));
                if (expiration_ms > 0) {
                    envelope.setExpiration(std::to_string(expiration_ms));
                }
                return channel_->publish(exchange, routing_key, envelope);
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
                bool retry_mode_enabled_ = false;
                std::string retry_exchange_name_;
                std::string retry_routing_key_;
                std::string dead_exchange_name_;
                std::string dead_routing_key_;
                uint32_t retry_delay_ms_ = 10000;
                uint32_t max_retry_count_ = 3;
    };

}