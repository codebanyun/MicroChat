//实现语音识别子服务
#pragma once
#include <brpc/server.h>
#include <butil/logging.h>

#include "speech.hpp"      // 语音识别模块封装
#include "etcd.hpp"     // 服务注册模块封装
#include "spdlog.hpp"   // 日志模块封装
#include "speech.pb.h"  // protobuf框架代码

namespace MicroChat { 
    class SpeechServiceImpl : public SpeechService {
        public:
            SpeechServiceImpl(const std::shared_ptr<MicroChat::SpeechClient>& asr_client) : asr_client_(asr_client) {}
            ~SpeechServiceImpl() override {}
            void SpeechRecognition(google::protobuf::RpcController* controller,
                                const MicroChat::SpeechRecognitionReq* request,
                                MicroChat::SpeechRecognitionRsp* response,
                                google::protobuf::Closure* done) override
            {
                brpc::ClosureGuard done_guard(done);
                // 语音识别逻辑
                std::string err_msg;
                std::string recognized_text = asr_client_->recognize(request->speech_content(), err_msg);
                if (recognized_text.empty()) {
                    response->set_request_id(request->request_id());
                    response->set_success(false);
                    response->set_errmsg(err_msg);
                    LOG_ERROR("语音识别失败: {}", err_msg);
                    return;
                }
                response->set_request_id(request->request_id());
                response->set_success(true);
                response->set_recognition_result(recognized_text);
                LOG_INFO("语音识别完成: {}", recognized_text);
            }
        private:
            std::shared_ptr<MicroChat::SpeechClient> asr_client_;
    };
    class SpeechServer{
        public:
            SpeechServer(const std::shared_ptr<MicroChat::SpeechClient>& asr_client,
                         const std::shared_ptr<MicroChat::EtcdClientRegistry>& etcd_registry,
                         const std::shared_ptr<brpc::Server>& brpc_server)
                : asr_client_(asr_client),
                  etcd_registry_(etcd_registry),
                  brpc_server_(brpc_server) 
                {}
            ~SpeechServer() {}
            //启动服务
            void Start() {
                brpc_server_ -> RunUntilAskedToQuit();
            }
        private:
            std::shared_ptr<MicroChat::SpeechClient> asr_client_;
            std::shared_ptr<MicroChat::EtcdClientRegistry> etcd_registry_;
            std::shared_ptr<brpc::Server> brpc_server_;
    };
    class SpeechServerFactory {
        public:
            void make_asr_object(const std::string &app_id,
                                 const std::string &api_key,
                                 const std::string &secret_key) {
                asr_client_ = std::make_shared<MicroChat::SpeechClient>(app_id, api_key, secret_key);
            }
            void make_etcd_registry(const std::string& etcd_host,
                                    const std::string& service_name,
                                    const std::string& service_host,
                                    int ttl = 3) {
                etcd_registry_ = std::make_shared<MicroChat::EtcdClientRegistry>(etcd_host);
                if (!etcd_registry_->register_service(service_name, service_host, ttl)) {
                    LOG_ERROR("Failed to register service with etcd");
                }
            }
            void make_brpc_server(uint16_t port , int rpc_threads , int rpc_timeout) {
                if (!asr_client_) {
                    LOG_ERROR("ASR client is not initialized");
                    return;
                }
                brpc_server_ = std::make_shared<brpc::Server>();
                MicroChat::SpeechServiceImpl* speech_service = new MicroChat::SpeechServiceImpl(asr_client_);
                if (brpc_server_->AddService(speech_service, brpc::ServiceOwnership::SERVER_OWNS_SERVICE) == -1) {
                    LOG_ERROR("Failed to add SpeechService to brpc server");
                }
                brpc::ServerOptions options;
                options.num_threads = rpc_threads;
                options.idle_timeout_sec = rpc_timeout;
                if (brpc_server_->Start(port, &options) == -1) {
                    LOG_ERROR("Failed to start brpc server on port {}", port);
                }
            }
            std::shared_ptr<MicroChat::SpeechServer> get_speech_server() {
                if(!asr_client_ || !etcd_registry_ || !brpc_server_) {
                    LOG_ERROR("SpeechServer components are not fully initialized");
                    return nullptr;
                }
                return std::make_shared<MicroChat::SpeechServer>(asr_client_, etcd_registry_, brpc_server_);
            }
        private:
            std::shared_ptr<MicroChat::SpeechClient> asr_client_;
            std::shared_ptr<MicroChat::EtcdClientRegistry> etcd_registry_;
            std::shared_ptr<brpc::Server> brpc_server_;
    };
}// namespace MicroChat