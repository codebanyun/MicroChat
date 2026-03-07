#pragma once 
#include <brpc/server.h>
#include <butil/logging.h>
#include <cctype>
#include <ctime>
#include "etcd.hpp"     // 服务注册模块封装
#include "spdlog.hpp"   // 日志模块封装
#include "utils.hpp"    // 基础工具接口
#include "channel.hpp"  // 信道管理模块封装
#include "rabbitmq.hpp"  // RabbitMQ模块封装
#include "mysql_chat_session_member.hpp"  // mysql数据管理客户端封装
#include "base.pb.h"  // protobuf框架代码
#include "user.pb.h"  // protobuf框架代码
#include "forward.pb.h"  // protobuf框架代码

namespace MicroChat {
    class MsgForwardServiceImpl : public MsgTransmitService {
    public:
        MsgForwardServiceImpl(
            const std::shared_ptr<RabbitMQClient> &rabbitmq_client,
            const std::shared_ptr<odb::core::database> &mysql_client,
            const std::shared_ptr<ServiceManager> &channel_manager,
            const std::string &user_service_name,
            const std::string &exchange_name,
            const std::string &routing_key)
            : rabbitmq_client_(rabbitmq_client),
              mysql_chat_session_member_(std::make_shared<ChatSessionMemberTable>(mysql_client)),
              service_manager_(channel_manager),
              user_service_name_(user_service_name),
              exchange_name_(exchange_name),
              routing_key_(routing_key) {}
        ~MsgForwardServiceImpl() override {}
        void GetTransmitTarget(google::protobuf::RpcController* controller,
                            const ::MicroChat::NewMessageReq* request,
                            ::MicroChat::GetTransmitTargetRsp* response,
                            google::protobuf::Closure* done) override {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
            };
            //1. 从请求中取出用户ID，所属会话ID，消息内容
            std::string user_id = request->user_id();
            std::string chat_session_id = request->chat_session_id();
            std::string request_id = request->request_id();
            const MessageContent &content = request->message();
            if (user_id.empty() || chat_session_id.empty() || !request->has_message()) {
                err_response(request_id, "用户ID、会话ID或消息内容不能为空");
                LOG_WARN("转发消息失败，用户ID、会话ID或消息内容不能为空");
                return;
            }
            //2. 获取消息发送者信息
            auto channel = service_manager_->get_service_node(user_service_name_);
            if (channel == nullptr) {
                err_response(request_id, "获取用户服务信道失败");
                LOG_ERROR("获取用户服务信道失败");
                return;
            }
            UserService_Stub user_stub(channel.get());
            GetUserInfoReq user_req;
            GetUserInfoRsp user_rsp;
            user_req.set_request_id(request_id);
            user_req.set_user_id(user_id);
            brpc::Controller cntl;
            user_stub.GetUserInfo(&cntl, &user_req, &user_rsp, nullptr);
            if (cntl.Failed() || !user_rsp.success()) {
                err_response(request_id, "获取用户信息失败，用户服务响应错误");
                LOG_WARN("获取用户信息失败，用户服务响应错误，用户ID：{}", user_id);
                return;
            }
            //构造转发消息
            MessageInfo msg_info;
            msg_info.mutable_sender()->CopyFrom(user_rsp.user_info());
            msg_info.mutable_message()->CopyFrom(content);
            msg_info.set_chat_session_id(chat_session_id);
            msg_info.set_message_id(UUID());
            msg_info.set_timestamp(time(nullptr));
            //3. 查询会话成员列表，构造转发目标用户列表
            auto members = mysql_chat_session_member_->getMembers(chat_session_id);
            if (members.empty()) {
                err_response(request_id, "会话成员列表为空，无法转发消息");
                LOG_WARN("会话成员列表为空，无法转发消息，会话ID：{}", chat_session_id);
                return; 
            }
            //4. 将消息发送到RabbitMQ交换机
            bool publish_result = rabbitmq_client_->publichMessage(
                exchange_name_, routing_key_, msg_info.SerializeAsString());
            if (!publish_result) {
                err_response(request_id, "发布转发消息到RabbitMQ失败");
                LOG_ERROR("发布转发消息到RabbitMQ失败");
                return;
            }
            //5. 返回成功响应，包含转发目标用户列表
            response->set_request_id(request_id);
            response->set_success(true);
            response->mutable_message()->CopyFrom(msg_info);
            for (const auto &member_id : members) {
                if (member_id != user_id) {
                    response->add_target_id_list(member_id);
                }
            }
            LOG_INFO("消息转发成功，消息ID：{}，会话ID：{}", msg_info.message_id(), chat_session_id);
        }
    private:
        std::string user_service_name_;//用户服务名称
        std::string exchange_name_; // 交换机名称
        std::string routing_key_; // 路由键
        std::shared_ptr<RabbitMQClient> rabbitmq_client_; // RabbitMQ客户端
        std::shared_ptr<ChatSessionMemberTable> mysql_chat_session_member_; // mysql聊天会话成员表
        std::shared_ptr<ServiceManager> service_manager_; //服务信道管理器
    };
    class ForwardServer {
    public:
        ForwardServer(
            const std::shared_ptr<EtcdClientfinder> &etcd_client_finder,
            const std::shared_ptr<EtcdClientRegistry> &etcd_client_registry,
            const std::shared_ptr<odb::core::database> &mysql_client,
            const std::shared_ptr<brpc::Server> &brpc_server
        ):
            etcd_client_finder_(etcd_client_finder),
            etcd_client_registry_(etcd_client_registry),
            mysql_client_(mysql_client),
            brpc_server_(brpc_server) {}

        ~ForwardServer() = default;
        void start() {
            brpc_server_ -> RunUntilAskedToQuit();
        }
    private:
        std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
        std::shared_ptr<EtcdClientRegistry> etcd_client_registry_;
        std::shared_ptr<odb::core::database> mysql_client_;
        std::shared_ptr<brpc::Server> brpc_server_;
    };
    class ForwardServerFactory {
    public:
        ForwardServerFactory() = default;
        ~ForwardServerFactory() = default;
        //构造mysql客户端
        bool create_mysql_client(
            const std::string& db_name,
            const std::string& user,
            const std::string& password,
            const std::string& host = "127.0.0.1",
            unsigned int port = 3306,
            const std::string &cset = "utf8",
            int conn_pool_count = 1) {
            mysql_client_ = MySQLDatabaseBuilder::createDatabase(
                db_name, user, password, host, port, cset, conn_pool_count);
            return mysql_client_ != nullptr;
        }
        //构造服务注册对象
        bool create_etcd_register_clients(
            const std::string &etcd_host,
            const std::string& service_name,
            const std::string& service_host) {
            etcd_client_registry_ = std::make_shared<EtcdClientRegistry>(etcd_host);
            if(!etcd_client_registry_) {
                LOG_ERROR("Failed to create EtcdClientRegistry");
                return false;
            }
            auto ret = etcd_client_registry_->register_service(
                service_name,
                service_host);
            return ret;
        }
        //构造服务发现对象与服务信道管理器
        bool create_etcd_finder_clients(
            const std::string &etcd_host,
            const std::string& base_service_name,
            const std::string& user_service_name) {
            user_service_name_ = user_service_name;
            service_manager_ = std::make_shared<ServiceManager>();
            if (!service_manager_) {
                LOG_ERROR("Failed to create ServiceManager");
                return false;
            }
            service_manager_->add_service_name(user_service_name);
            auto sm = service_manager_;
            auto put_cb = [sm](const std::string& instance, const std::string& addr) {
                sm->on_service_online(instance, addr);
            };
            auto del_cb = [sm](const std::string& instance, const std::string& addr) {
                sm->on_service_offline(instance, addr);
            };
            etcd_client_finder_ = std::make_shared<EtcdClientfinder>(etcd_host , base_service_name , put_cb, del_cb);
            return etcd_client_finder_ != nullptr;
        }
        bool create_rabbitmq_client(
            const std::string &host,
            const std::string &user,
            const std::string &passwd,
            const std::string &exchange,
            const std::string &queue,
            const std::string &binding_key) {
            rabbitmq_client_ = std::make_shared<RabbitMQClient>(user, passwd , host);
            if (!rabbitmq_client_) return false;
            rabbitmq_client_->declareComponents(exchange, queue, binding_key);
            return true;
        }
        // 构造rpc服务器
        bool create_brpc_server(uint16_t port , int timeout , int thread_num,
            const std::string &exchange_name,
            const std::string &routing_key) {
            if(!mysql_client_  || !rabbitmq_client_ || !service_manager_) {
                LOG_ERROR("Failed to create brpc server, dependencies are not satisfied");
                return false;
            }
            brpc_server_ = std::make_shared<brpc::Server>();
            MsgForwardServiceImpl* msg_forward_service_impl = new MsgForwardServiceImpl(
                rabbitmq_client_,
                mysql_client_,
                service_manager_,
                user_service_name_,
                exchange_name,  // 交换机名称
                routing_key // 路由键
            );
            if (brpc_server_->AddService(msg_forward_service_impl, brpc::SERVER_OWNS_SERVICE) != 0) {
                LOG_ERROR("Failed to add MsgForwardService to brpc server");
                return false;
            }
            brpc::ServerOptions options;
            options.idle_timeout_sec = timeout;
            options.num_threads = thread_num;
            if (brpc_server_->Start(port, &options) != 0) {
                LOG_ERROR("Failed to start brpc server on port {}", port);
                return false;
            }
            return true;
        }
        //构造转发服务器
        std::shared_ptr<ForwardServer> build() {
            if(!mysql_client_ || !etcd_client_finder_ || 
               !etcd_client_registry_ || !brpc_server_) {
                LOG_ERROR("Failed to create ForwardServer, dependencies are not satisfied");
                return nullptr;
            }
            return  std::make_shared<ForwardServer>(
                etcd_client_finder_,
                etcd_client_registry_,
                mysql_client_,
                brpc_server_);
        }
    private:
        std::string user_service_name_;//用户服务名称
        std::string exchange_name_; // 交换机名称
        std::string routing_key_; // 路由键
        std::shared_ptr<RabbitMQClient> rabbitmq_client_; // RabbitMQ客户端
        std::shared_ptr<odb::core::database> mysql_client_; // mysql数据库客户端
        std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
        std::shared_ptr<EtcdClientRegistry> etcd_client_registry_;
        std::shared_ptr<ServiceManager> service_manager_; //服务信道管理器
        std::shared_ptr<brpc::Server> brpc_server_;
    };
} // namespace MicroChat