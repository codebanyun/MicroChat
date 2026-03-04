#pragma once
#include "channel.hpp"
#include "etcd.hpp"
#include "spdlog.hpp"
#include "redis_manager.hpp"
#include "connection.hpp"
#include "utils.hpp"
#include "httplib.h"
#include <cstddef>

#include "base.pb.h"
#include "file.pb.h"
#include "forward.pb.h"
#include "friend.pb.h"
#include "gateway.pb.h"
#include "user.pb.h"
#include "message.pb.h"
#include "notify.pb.h"
#include "speech.pb.h"

namespace MicroChat {
    #define GET_PHONE_VERIFY_CODE   "/service/user/get_phone_verify_code"
    #define USERNAME_REGISTER       "/service/user/username_register"
    #define USERNAME_LOGIN          "/service/user/username_login"
    #define PHONE_REGISTER          "/service/user/phone_register"
    #define PHONE_LOGIN             "/service/user/phone_login"
    #define GET_USERINFO            "/service/user/get_user_info"
    #define SET_USER_AVATAR         "/service/user/set_avatar"
    #define SET_USER_NICKNAME       "/service/user/set_nickname"
    #define SET_USER_DESC           "/service/user/set_description"
    #define SET_USER_PHONE          "/service/user/set_phone"
    #define SET_USER_PASSWARD       "/service/user/set_password"
    #define FRIEND_GET_LIST         "/service/friend/get_friend_list"
    #define FRIEND_APPLY            "/service/friend/add_friend_apply"
    #define FRIEND_APPLY_PROCESS    "/service/friend/add_friend_process"
    #define FRIEND_REMOVE           "/service/friend/remove_friend"
    #define FRIEND_SEARCH           "/service/friend/search_friend"
    #define FRIEND_GET_PENDING_EV   "/service/friend/get_pending_friend_events"
    #define CSS_GET_LIST            "/service/friend/get_chat_session_list"
    #define CSS_CREATE              "/service/friend/create_chat_session"
    #define CSS_GET_MEMBER          "/service/friend/get_chat_session_member"
    #define MSG_GET_RANGE           "/service/message_storage/get_history"
    #define MSG_GET_RECENT          "/service/message_storage/get_recent"
    #define MSG_KEY_SEARCH          "/service/message_storage/search_history"
    #define NEW_MESSAGE             "/service/message_transmit/new_message"
    #define FILE_GET_SINGLE         "/service/file/get_single_file"
    #define FILE_GET_MULTI          "/service/file/get_multi_file"
    #define FILE_PUT_SINGLE         "/service/file/put_single_file"
    #define FILE_PUT_MULTI          "/service/file/put_multi_file"
    #define SPEECH_RECOGNITION      "/service/speech/recognition"
    class GatewayServer {
        public:
            using server_t = Connection::server_t;
            GatewayServer(const std::shared_ptr<sw::redis::Redis> &redis_client,
                          const std::shared_ptr<ServiceManager> &service_manager,
                          std::shared_ptr<EtcdClientfinder> etcd_client_finder,
                          const std::string user_service_name,
                          const std::string file_service_name,
                          const std::string speech_service_name,
                          const std::string message_service_name,
                          const std::string forward_service_name,
                          const std::string friend_service_name,
                          int websocket_port,
                          int http_port)
                : redis_session_(std::make_shared<Session>(redis_client)),
                  redis_LoginStatus_(std::make_shared<LoginStatus>(redis_client)), 
                  service_manager_(service_manager), etcd_client_finder_(etcd_client_finder),
                  user_service_name_(user_service_name), file_service_name_(file_service_name),
                  speech_service_name_(speech_service_name), message_service_name_(message_service_name),
                  forward_service_name_(forward_service_name), friend_service_name_(friend_service_name), 
                  websocket_port_(websocket_port), http_port_(http_port),
                  connection_manager_(std::make_shared<Connection>()) {
                    websocket_server_.set_access_channels(websocketpp::log::alevel::none);
                    websocket_server_.init_asio();
                    websocket_server_.set_open_handler(std::bind(&GatewayServer::on_open, this, std::placeholders::_1));
                    websocket_server_.set_close_handler(std::bind(&GatewayServer::on_close, this, std::placeholders::_1));
                    websocket_server_.set_message_handler(std::bind(&GatewayServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
                    websocket_server_.set_reuse_addr(true);
                    websocket_server_.listen(websocket_port_);
                    websocket_server_.set_pong_handler(std::bind(&GatewayServer::on_pong, this, std::placeholders::_1, std::placeholders::_2));
                    websocket_server_.start_accept();
                    auto bind_handler = [this](auto method) {
                            return httplib::Server::Handler(
                                    std::bind(method, this, std::placeholders::_1, std::placeholders::_2));
                    };
                    http_server_.set_default_headers({
                        {"Access-Control-Allow-Origin", "*"},
                        {"Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS"},
                        {"Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With"},
                        {"Access-Control-Max-Age", "86400"}
                    });
                    http_server_.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
                        if (req.method == "OPTIONS") {
                            res.status = 204;
                            return httplib::Server::HandlerResponse::Handled;
                        }
                        return httplib::Server::HandlerResponse::Unhandled;
                    });
                    http_server_.Post(GET_PHONE_VERIFY_CODE, bind_handler(&GatewayServer::GetPhoneVerifyCode));
                    http_server_.Post(USERNAME_REGISTER, bind_handler(&GatewayServer::UserRegister));
                    http_server_.Post(USERNAME_LOGIN, bind_handler(&GatewayServer::UsernameLogin));
                    http_server_.Post(PHONE_REGISTER, bind_handler(&GatewayServer::PhoneRegister));
                    http_server_.Post(PHONE_LOGIN, bind_handler(&GatewayServer::PhoneLogin));
                    http_server_.Post(GET_USERINFO, bind_handler(&GatewayServer::GetUserInfo));
                    http_server_.Post(SET_USER_AVATAR, bind_handler(&GatewayServer::SetUserAvatar));
                    http_server_.Post(SET_USER_NICKNAME, bind_handler(&GatewayServer::SetUserNickname));
                    http_server_.Post(SET_USER_DESC, bind_handler(&GatewayServer::SetUserDesc));
                    http_server_.Post(SET_USER_PHONE, bind_handler(&GatewayServer::SetUserPhone));
                    http_server_.Post(SET_USER_PASSWARD, bind_handler(&GatewayServer::SetUserPassword));
                    http_server_.Post(FRIEND_GET_LIST, bind_handler(&GatewayServer::GetFriendList));
                    http_server_.Post(FRIEND_APPLY, bind_handler(&GatewayServer::FriendApply));
                    http_server_.Post(FRIEND_APPLY_PROCESS, bind_handler(&GatewayServer::FriendApplyProcess));
                    http_server_.Post(FRIEND_REMOVE, bind_handler(&GatewayServer::FriendRemove));
                    http_server_.Post(FRIEND_SEARCH, bind_handler(&GatewayServer::FriendSearch));
                    http_server_.Post(FRIEND_GET_PENDING_EV, bind_handler(&GatewayServer::GetPendingFriendEvents));
                    http_server_.Post(CSS_GET_LIST, bind_handler(&GatewayServer::GetChatSessionList));
                    http_server_.Post(CSS_CREATE, bind_handler(&GatewayServer::CreateChatSession));
                    http_server_.Post(CSS_GET_MEMBER, bind_handler(&GatewayServer::GetChatSessionMember));
                    http_server_.Post(MSG_GET_RANGE, bind_handler(&GatewayServer::GetHistoryMsg));
                    http_server_.Post(MSG_GET_RECENT, bind_handler(&GatewayServer::GetRecentMsg));
                    http_server_.Post(MSG_KEY_SEARCH, bind_handler(&GatewayServer::SearchHistoryMsg));
                    http_server_.Post(NEW_MESSAGE, bind_handler(&GatewayServer::NewMessage));
                    http_server_.Post(FILE_GET_SINGLE, bind_handler(&GatewayServer::GetSingleFile));
                    http_server_.Post(FILE_GET_MULTI, bind_handler(&GatewayServer::GetMultiFile));
                    http_server_.Post(FILE_PUT_SINGLE, bind_handler(&GatewayServer::PutSingleFile));
                    http_server_.Post(FILE_PUT_MULTI, bind_handler(&GatewayServer::PutMultiFile));
                    http_server_.Post(SPEECH_RECOGNITION, bind_handler(&GatewayServer::SpeechRecognition));
                    http_thread_ = std::thread([this](){
                        try {
                            http_server_.listen("0.0.0.0", http_port_);
                        } catch (const std::exception& e) {
                            spdlog::error("HTTP server exception: {}", e.what());
                        }
                    });
                    http_thread_.detach();
                }
            ~GatewayServer() = default;
        private:
            //connection的weak_ptr
            void on_open(websocketpp::connection_hdl hdl) {
                LOG_INFO("websocket 长连接建立");
            }
            void on_close(websocketpp::connection_hdl hdl) {
                LOG_INFO("websocket 长连接断开");
                auto conn = websocket_server_.get_con_from_hdl(hdl); //获取weak_ptr的shared_ptr
                //1.通过连接句柄获取用户信息
                std::string session_id, user_id;
                bool ret = connection_manager_->getClient(conn , session_id, user_id);
                if (!ret) {
                    LOG_WARN("未找到长连接句柄: {}", reinterpret_cast<std::size_t>(conn.get()));
                    return;
                }
                //2.删除用户在线状态与session信息
                if (!user_id.empty()) {
                    redis_LoginStatus_->deleteuser(user_id);
                }
                if (!session_id.empty()) {
                    redis_session_->delete_session(session_id);
                }
                //3. 删除连接管理器中的连接记录
                connection_manager_->remove(conn);
                LOG_INFO("长连接断开处理完成，user_id: {}, session_id: {}", user_id, session_id);
            }
            // 收到 pong 时更新时间
            void on_pong(websocketpp::connection_hdl hdl, std::string payload) {
                server_t::connection_ptr conn = websocket_server_.get_con_from_hdl(hdl);
                if (!conn) return;

                std::lock_guard<std::mutex> lock(pong_mtx_);
                last_pong_time_[conn] = std::chrono::steady_clock::now();
            }
            void _keepalive(server_t::connection_ptr conn) {
                std::weak_ptr<server_t::connection_type> weak_conn = conn;
                
                conn->set_timer(30000, [this, weak_conn](websocketpp::lib::error_code ec) {
                    if (ec) return;

                    auto conn = weak_conn.lock();
                    if (!conn || conn->get_state() != websocketpp::session::state::open) return;

                    // 检查是否超过 90 秒未收到 pong（3 个心跳周期）
                    {
                        std::lock_guard<std::mutex> lock(pong_mtx_);
                        auto it = last_pong_time_.find(conn);
                        if (it != last_pong_time_.end()) {
                            auto now = std::chrono::steady_clock::now();
                            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() > 90) {
                                LOG_WARN("心跳超时，关闭连接，conn: {}", reinterpret_cast<std::size_t>(conn.get()));
                                websocket_server_.close(conn->get_handle(), 
                                    websocketpp::close::status::going_away, "Heartbeat timeout"); //1001 端点离开
                                last_pong_time_.erase(it);
                                return;
                            }
                        }
                    }

                    // 发送 ping 并继续循环
                    conn->ping("");
                    _keepalive(conn);
                });
            }
            void on_message(websocketpp::connection_hdl hdl, server_t::message_ptr msg) {
                LOG_INFO("websocket 长连接收到消息，payload: {}", msg->get_payload());
                auto conn = websocket_server_.get_con_from_hdl(hdl); //获取weak_ptr的shared_ptr
                //1. 对消息反序列化
                ClientAuthenticationReq req;
                if (!req.ParseFromString(msg->get_payload())) {
                    LOG_WARN("消息反序列化失败，payload: {}", msg->get_payload());
                    websocket_server_.close(hdl, websocketpp::close::status::unsupported_data, "消息格式错误，无法解析");
                    return;
                }
                //2. 通过连接句柄获取用户信息
                std::string session_id = req.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    websocket_server_.close(hdl, websocketpp::close::status::normal, "鉴权失败，session_id无效");
                    return;
                }
                //3. 将连接信息保存到连接管理器中
                connection_manager_->insert(user_id, session_id, conn);
                _keepalive(conn);
                LOG_INFO("新增长连接管理，user_id: {}, session_id: {}", user_id, session_id);
            }
            void GetPhoneVerifyCode(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                PhoneVerifyCodeReq request;
                PhoneVerifyCodeRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetPhoneVerifyCode(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //3. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetPhoneVerifyCode请求完成，phone: {}, success: {}", request.phone_number(), response.success());
            }
            void UserRegister(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                UserRegisterReq request;
                UserRegisterRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 通过etcd获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.UserRegister(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //3. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理UserRegister请求完成，nickname: {}, success: {}", request.nickname(), response.success());
            }
            void UsernameLogin(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                UserLoginReq request;
                UserLoginRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.UserLogin(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //3. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理UserLogin请求完成，nickname: {}, success: {}", request.nickname(), response.success());
            }
            void PhoneRegister(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                PhoneRegisterReq request;
                PhoneRegisterRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.PhoneRegister(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //3. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理PhoneRegister请求完成，phone: {}, success: {}", request.phone_number(), response.success());
            }
            void PhoneLogin(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                PhoneLoginReq request;
                PhoneLoginRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.PhoneLogin(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //3. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理PhoneLogin请求完成，phone: {}, success: {}", request.phone_number(), response.success());
            }
            void GetUserInfo(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetUserInfoReq request;
                GetUserInfoRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //2. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetUserInfo(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //3. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetUserInfo请求完成，user_id: {}, success: {}", request.user_id(), response.success());
            }
            void SetUserAvatar(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                SetUserAvatarReq request;
                SetUserAvatarRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.SetUserAvatar(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //4. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理SetUserAvatar请求完成，user_id: {}, success: {}", user_id, response.success());
            }
            void SetUserNickname(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                SetUserNicknameReq request;
                SetUserNicknameRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.SetUserNickname(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //4. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理SetUserNickname请求完成，user_id: {}, success: {}", user_id, response.success());
            }
            void SetUserDesc(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                SetUserDescriptionReq request;
                SetUserDescriptionRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.SetUserDescription(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //4. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理SetUserDesc请求完成，user_id: {}, success: {}", user_id, response.success());
            }
            void SetUserPhone(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                SetUserPhoneNumberReq request;
                SetUserPhoneNumberRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.SetUserPhoneNumber(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //4. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理SetUserPhone请求完成，user_id: {}, success: {}", user_id, response.success());
            }
            void SetUserPassword(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                SetUserPasswordReq request;
                SetUserPasswordRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    err_response("用户服务不可用");
                    return;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.SetUserPassword(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    err_response("调用用户服务失败");
                    return;
                }
                //4. 将用户服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理SetUserPassword请求完成，user_id: {}, success: {}", user_id, response.success());
            }
            void GetFriendList(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetFriendListReq request;
                GetFriendListRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetFriendList(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 将好友服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetFriendList请求完成，user_id: {}, success: {}, friend_count: {}", user_id, response.success(), response.friend_list_size());
            }
            std::shared_ptr<GetUserInfoRsp> _GetUserInfo(const std::string &rid, const std::string &uid) {
                GetUserInfoReq request;
                request.set_request_id(rid);
                request.set_user_id(uid);
                // 通过service_manager获取用户服务的地址，将请求转发给用户服务
                auto channel = service_manager_->get_service_node(user_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到用户服务地址，service_name: {}", user_service_name_);
                    return nullptr;
                }
                MicroChat::UserService_Stub stub(channel.get());
                brpc::Controller cntl;
                GetUserInfoRsp response;
                stub.GetUserInfo(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用用户服务失败，error: {}", cntl.ErrorText());
                    return nullptr;
                }
                LOG_INFO("获取用户信息成功，user_id: {}", uid);
                return std::make_shared<GetUserInfoRsp>(response);
            }
            void FriendApply(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                FriendAddReq request;
                FriendAddRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.FriendAdd(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 向被申请人发送好友申请通知
                auto conn = connection_manager_->getConnection(request.recipient_id());
                if (response.success() && conn) {
                    auto user_info = _GetUserInfo(request.request_id(), user_id);
                    if (!user_info) {
                        LOG_ERROR("获取发送者用户信息失败，user_id: {}", user_id);
                        err_response("获取用户信息失败");
                        return;
                    }
                    NotifyMessage notify;
                    notify.set_notify_type(NotifyType::FRIEND_ADD_APPLY_NOTIFY);
                    notify.mutable_friend_add_apply()->mutable_user_info()->CopyFrom(user_info->user_info());
                    conn->send(notify.SerializeAsString(), websocketpp::frame::opcode::value::binary);
                }
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理FriendAdd请求完成，user_id: {}, target_user_id: {}, success: {}", user_id, request.recipient_id(), response.success());
            }
            void FriendApplyProcess(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                FriendAddProcessReq request;
                FriendAddProcessRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_recipient_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.FriendAddProcess(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 向申请人发送好友申请处理结果通知，为双方创建聊天会话
                if(response.success()) {
                    auto user_info = _GetUserInfo(request.request_id(), user_id);
                    if (!user_info) {
                        LOG_ERROR("获取处理者用户信息失败，user_id: {}", user_id);
                        err_response("获取用户信息失败");
                        return;
                    }
                    auto requester_info = _GetUserInfo(request.request_id(), request.sender_id());
                    if (!requester_info) {
                        LOG_ERROR("获取申请人用户信息失败，user_id: {}", request.sender_id());
                        err_response("获取用户信息失败");
                        return;
                    }
                    // 向申请人发送处理结果通知
                    auto requester_conn = connection_manager_->getConnection(request.sender_id());
                    if (requester_conn) {
                        NotifyMessage notify;
                        notify.set_notify_type(NotifyType::FRIEND_ADD_PROCESS_NOTIFY);
                        notify.mutable_friend_process_result()->set_agree(request.agree());
                        notify.mutable_friend_process_result()->mutable_user_info()->CopyFrom(user_info->user_info());
                        requester_conn->send(notify.SerializeAsString(), websocketpp::frame::opcode::value::binary);
                        LOG_INFO("向申请人发送好友申请处理结果通知，applicant_user_id: {}, processor_user_id: {}, agree: {}", request.sender_id(), user_id, request.agree());
                    }
                    // 如果同意好友申请，为双方创建聊天会话
                    if (request.agree()) {
                        auto user_conn = connection_manager_->getConnection(request.recipient_id());
                        if (requester_conn) {
                            NotifyMessage notify;
                            notify.set_notify_type(NotifyType::CHAT_SESSION_CREATE_NOTIFY);
                            auto chat_session_info = notify.mutable_new_chat_session_info();
                            chat_session_info->mutable_chat_session_info()->set_single_chat_friend_id(request.sender_id());
                            chat_session_info->mutable_chat_session_info()->set_chat_session_id(response.new_session_id());
                            chat_session_info->mutable_chat_session_info()->set_chat_session_name(user_info->user_info().nickname());
                            chat_session_info->mutable_chat_session_info()->set_avatar(user_info->user_info().avatar());
                            user_conn->send(notify.SerializeAsString(), websocketpp::frame::opcode::value::binary);
                            LOG_INFO("向申请人发送新聊天会话通知，recipient_user_id: {}, session_id: {}", request.recipient_id(), response.new_session_id());
                        }
                        if(user_conn) {
                            NotifyMessage notify;
                            notify.set_notify_type(NotifyType::CHAT_SESSION_CREATE_NOTIFY);
                            auto chat_session_info = notify.mutable_new_chat_session_info();
                            chat_session_info->mutable_chat_session_info()->set_single_chat_friend_id(request.recipient_id());
                            chat_session_info->mutable_chat_session_info()->set_chat_session_id(response.new_session_id());
                            chat_session_info->mutable_chat_session_info()->set_chat_session_name(requester_info->user_info().nickname());
                            chat_session_info->mutable_chat_session_info()->set_avatar(requester_info->user_info().avatar());
                            requester_conn->send(notify.SerializeAsString(), websocketpp::frame::opcode::value::binary);
                            LOG_INFO("向被申请人发送新聊天会话通知，recipient_user_id: {}, session_id: {}", request.sender_id(), response.new_session_id());
                        }
                    }
                }
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
            }
            void FriendRemove(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                FriendRemoveReq request;
                FriendRemoveRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.FriendRemove(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 向被删除人发送好友删除通知
                auto conn = connection_manager_->getConnection(request.friend_id());
                if (response.success() && conn) {
                    auto user_info = _GetUserInfo(request.request_id(), user_id);
                    if (!user_info) {
                        LOG_ERROR("获取发送者用户信息失败，user_id: {}", user_id);
                        err_response("获取用户信息失败");
                        return;
                    }
                    NotifyMessage notify;
                    notify.set_notify_type(NotifyType::FRIEND_REMOVE_NOTIFY);
                    notify.mutable_friend_remove()->set_user_id(user_id);
                    conn->send(notify.SerializeAsString(), websocketpp::frame::opcode::value::binary);
                }
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理FriendRemove请求完成，user_id: {}, friend_id: {}, success: {}", user_id, request.friend_id(), response.success());
            }
            void FriendSearch(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                FriendSearchReq request;
                FriendSearchRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.FriendSearch(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 将好友服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理FriendSearch请求完成，user_id: {}, keyword: {}, success: {}, result_count: {}", user_id, request.search_key(), response.success(), response.user_info_size());
            }
            void GetPendingFriendEvents(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetPendingFriendEventListReq request;
                GetPendingFriendEventListRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetPendingFriendEventList(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 将好友服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetPendingFriendEventList请求完成，user_id: {}, success: {}, event_count: {}", user_id, response.success(), response.friend_event_info_list_size());
            }
            void GetChatSessionList(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetChatSessionListReq request;
                GetChatSessionListRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetChatSessionList(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 将消息服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetChatSessionList请求完成，user_id: {}, success: {}, session_count: {}", user_id, response.success(), response.chat_session_info_list_size());
            }
            void CreateChatSession(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                ChatSessionCreateReq request;
                ChatSessionCreateRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.ChatSessionCreate(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 将消息服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理CreateChatSession请求完成，user_id: {}, success: {}", user_id, response.success());
            }
            void GetChatSessionMember(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetChatSessionMemberReq request;
                GetChatSessionMemberRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取好友服务的地址，将请求转发给好友服务
                auto channel = service_manager_->get_service_node(friend_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到好友服务地址，service_name: {}", friend_service_name_);
                    err_response("好友服务不可用");
                    return;
                }
                MicroChat::FriendService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetChatSessionMember(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用好友服务失败，error: {}", cntl.ErrorText());
                    err_response("调用好友服务失败");
                    return;
                }
                //4. 将消息服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetChatSessionMember请求完成，user_id: {}, session_id: {}, success: {}, member_count: {}", user_id, request.chat_session_id(), response.success(), response.member_info_list_size());
            }
            void GetHistoryMsg(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetHistoryMsgReq request;
                GetHistoryMsgRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取消息服务的地址，将请求转发给消息服务
                auto channel = service_manager_->get_service_node(message_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到消息服务地址，service_name: {}", message_service_name_);
                    err_response("消息服务不可用");
                    return;
                }
                MicroChat::MsgStorageService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetHistoryMsg(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用消息服务失败，error: {}", cntl.ErrorText());
                    err_response("调用消息服务失败");
                    return;
                }
                //4. 将消息服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetHistoryMsg请求完成，user_id: {}, session_id: {}, success: {}", user_id, request.session_id(), response.success());
            }
            void GetRecentMsg(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetRecentMsgReq request;
                GetRecentMsgRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取消息服务的地址，将请求转发给消息服务
                auto channel = service_manager_->get_service_node(message_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到消息服务地址，service_name: {}", message_service_name_);
                    err_response("消息服务不可用");
                    return;
                }
                MicroChat::MsgStorageService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetRecentMsg(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用消息服务失败，error: {}", cntl.ErrorText());
                    err_response("调用消息服务失败");
                    return;
                }
                //4. 将消息服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetRecentMsg请求完成，user_id: {}, session_id: {}, success: {}, msg_count: {}", user_id, request.session_id(), response.success(), response.msg_list_size());
            }
            void SearchHistoryMsg(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                MsgSearchReq request;
                MsgSearchRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取消息服务的地址，将请求转发给消息服务
                auto channel = service_manager_->get_service_node(message_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到消息服务地址，service_name: {}", message_service_name_);
                    err_response("消息服务不可用");
                    return;
                }
                MicroChat::MsgStorageService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.MsgSearch(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用消息服务失败，error: {}", cntl.ErrorText());
                    err_response("调用消息服务失败");
                    return;
                }
                //4. 将消息服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理SearchHistoryMsg请求完成，user_id: {}, session_id: {}, keyword: {}, success: {}, msg_count: {}", user_id, request.session_id(), request.search_key(), response.success(), response.msg_list_size());
            }
            void NewMessage(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                NewMessageReq request;
                NewMessageRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取转发服务的地址，将请求转发给转发服务
                auto channel = service_manager_->get_service_node(forward_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到转发服务地址，service_name: {}", forward_service_name_);
                    err_response("转发服务不可用");
                    return;
                }
                MicroChat::MsgTransmitService_Stub stub(channel.get());
                brpc::Controller cntl;
                GetTransmitTargetRsp transmit_response;
                stub.GetTransmitTarget(&cntl, &request, &transmit_response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用转发服务失败，error: {}", cntl.ErrorText());
                    err_response("调用转发服务失败");
                    return;
                }
                response.set_request_id(transmit_response.request_id());
                response.set_success(transmit_response.success());
                response.set_errmsg(transmit_response.errmsg());
                if (transmit_response.success()) {
                    NotifyMessage notify;
                    notify.set_notify_type(NotifyType::CHAT_MESSAGE_NOTIFY);
                    notify.mutable_new_message_info()->mutable_message_info()->CopyFrom(transmit_response.message());
                    std::string payload = notify.SerializeAsString();

                    for (const auto &target_id : transmit_response.target_id_list()) {
                        auto target_conn = connection_manager_->getConnection(target_id);
                        if (!target_conn) {
                            continue;
                        }
                        if(target_id == user_id) {
                            continue; // 消息发送者不需要接收自己发送的消息通知
                        }
                        target_conn->send(payload, websocketpp::frame::opcode::value::binary);
                    }
                }
                //4. 将消息服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理NewMessage请求完成，sender_user_id: {}, session_id: {}, success: {}", user_id, request.session_id(), response.success());
            }
            void GetSingleFile(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetSingleFileReq request;
                GetSingleFileRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取文件服务的地址，将请求转发给文件服务
                auto channel = service_manager_->get_service_node(file_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到文件服务地址，service_name: {}", file_service_name_);
                    err_response("文件服务不可用");
                    return;
                }
                MicroChat::FileService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetSingleFile(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用文件服务失败，error: {}", cntl.ErrorText());
                    err_response("调用文件服务失败");
                    return;
                }
                //4. 将文件服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetSingleFile请求完成，user_id: {}, file_id: {}, success: {}", user_id, request.file_id(), response.success());
            }
            void GetMultiFile(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                GetMultiFileReq request;
                GetMultiFileRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取文件服务的地址，将请求转发给文件服务
                auto channel = service_manager_->get_service_node(file_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到文件服务地址，service_name: {}", file_service_name_);
                    err_response("文件服务不可用");
                    return;
                }
                MicroChat::FileService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.GetMultiFile(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用文件服务失败，error: {}", cntl.ErrorText());
                    err_response("调用文件服务失败");
                    return;
                }
                //4. 将文件服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理GetMultiFile请求完成，user_id: {}, success: {}, file_count: {}", user_id, response.success(), response.file_data_size());
            }
            void PutSingleFile(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                PutSingleFileReq request;
                PutSingleFileRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取文件服务的地址，将请求转发给文件服务
                auto channel = service_manager_->get_service_node(file_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到文件服务地址，service_name: {}", file_service_name_);
                    err_response("文件服务不可用");
                    return;
                }
                MicroChat::FileService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.PutSingleFile(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用文件服务失败，error: {}", cntl.ErrorText());
                    err_response("调用文件服务失败");
                    return;
                }
                //4. 将文件服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理PutSingleFile请求完成，user_id: {}, success: {}, file_id: {}", user_id, response.success(), response.file_info().file_id());
            }
            void PutMultiFile(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                PutMultiFileReq request;
                PutMultiFileRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取文件服务的地址，将请求转发给文件服务
                auto channel = service_manager_->get_service_node(file_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到文件服务地址，service_name: {}", file_service_name_);
                    err_response("文件服务不可用");
                    return;
                }
                MicroChat::FileService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.PutMultiFile(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用文件服务失败，error: {}", cntl.ErrorText());
                    err_response("调用文件服务失败");
                    return;
                }
                //4. 将文件服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理PutMultiFile请求完成，user_id: {}, success: {}, file_count: {}", user_id, response.success(), response.file_info_size());
            }
            void SpeechRecognition(const httplib::Request& req, httplib::Response& res) {
                //1. 取出http请求正文，将正文进行反序列化
                SpeechRecognitionReq request;
                SpeechRecognitionRsp response;
                auto err_response = [&req, &response, &res](const std::string &errmsg) -> void {
                    response.set_success(false);
                    response.set_errmsg(errmsg);
                    res.set_content(response.SerializeAsString(), "application/x-protobuf");
                };
                if (!request.ParseFromString(req.body)) {
                    LOG_ERROR("消息反序列化失败，body: {}", req.body);
                    err_response("消息格式错误，无法解析");
                    return;
                }
                //2. 身份验证：从请求中获取session_id，查询redis获取对应的user_id，确保用户已登录
                std::string session_id = request.session_id();
                std::string user_id = redis_session_->get_uid(session_id);
                if (user_id.empty()) {
                    LOG_WARN("session_id无效，未找到对应用户，session_id: {}", session_id);
                    err_response("鉴权失败，session_id无效");
                    return;
                }
                request.set_user_id(user_id);
                //3. 通过service_manager获取语音服务的地址，将请求转发给语音服务
                auto channel = service_manager_->get_service_node(speech_service_name_);
                if (!channel) {
                    LOG_ERROR("未找到语音服务地址，service_name: {}", speech_service_name_);
                    err_response("语音服务不可用");
                    return;
                }
                MicroChat::SpeechService_Stub stub(channel.get());
                brpc::Controller cntl;
                stub.SpeechRecognition(&cntl, &request, &response, nullptr);
                if (cntl.Failed()) {
                    LOG_ERROR("调用语音服务失败，error: {}", cntl.ErrorText());
                    err_response("调用语音服务失败");
                    return;
                }
                //4. 将语音服务的响应进行序列化，返回给http客户端
                res.set_content(response.SerializeAsString(), "application/x-protobuf");
                LOG_INFO("处理SpeechRecognition请求完成，user_id: {}, success: {}, text: {}", user_id, response.success(), response.recognition_result());
            }
        public:
            void start() {
                websocket_server_.run();
            }
        private:
            std::shared_ptr<Session> redis_session_;
            std::shared_ptr<LoginStatus> redis_LoginStatus_;
            std::shared_ptr<ServiceManager> service_manager_;
            std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
            std::string user_service_name_;
            std::string file_service_name_;
            std::string speech_service_name_;
            std::string message_service_name_;
            std::string forward_service_name_;
            std::string friend_service_name_;
            std::shared_ptr<Connection> connection_manager_;
            int websocket_port_;
            int http_port_;
            server_t websocket_server_;
            httplib::Server http_server_;
            std::thread http_thread_;

            // 记录每个连接的最后 pong 时间
            std::unordered_map<server_t::connection_ptr, std::chrono::steady_clock::time_point> last_pong_time_;
            std::mutex pong_mtx_;
    };
    class GatewayServerBuilder {
        public:
            //构造服务发现对象与服务信道管理器
        bool create_etcd_finder_clients(
            const std::string &etcd_host,
            const std::string& base_service_name,
            const std::string& user_service_name,
            const std::string& message_service_name,
            const std::string& friend_service_name,
            const std::string& file_service_name,
            const std::string& speech_service_name,
            const std::string& forward_service_name) {
            user_service_name_ = user_service_name;
            message_service_name_ = message_service_name;
            friend_service_name_ = friend_service_name;
            file_service_name_ = file_service_name;
            speech_service_name_ = speech_service_name;
            forward_service_name_ = forward_service_name;
            service_manager_ = std::make_shared<ServiceManager>();
            if (!service_manager_) {
                LOG_ERROR("Failed to create ServiceManager");
                return false;
            }
            service_manager_->add_service_name(user_service_name_);
            service_manager_->add_service_name(message_service_name_);
            service_manager_->add_service_name(friend_service_name_);
            service_manager_->add_service_name(file_service_name_);
            service_manager_->add_service_name(speech_service_name_);
            service_manager_->add_service_name(forward_service_name_);
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
        //构造redis客户端
        bool create_redis_client(
            const std::string &host,
            int port,
            int db,
            bool keep_alive) {
            redis_client_ = RedisClientBuilder::build(host , port, db, keep_alive);
            return redis_client_ != nullptr;
        }
        void server_config(
            int websocket_port,
            int http_port) {
            websocket_port_ = websocket_port;
            http_port_ = http_port;
        }
        std::shared_ptr<GatewayServer> build() {
            if (!redis_client_ || !service_manager_ || !etcd_client_finder_) {
                LOG_ERROR("Failed to build GatewayServer: missing required clients");
                return nullptr;
            }
            auto gateway_server = std::make_shared<GatewayServer>(
                redis_client_,
                service_manager_,
                etcd_client_finder_,
                user_service_name_,
                file_service_name_,
                speech_service_name_,
                message_service_name_,
                forward_service_name_,
                friend_service_name_,
                websocket_port_,
                http_port_);
            if (!gateway_server) {
                LOG_ERROR("Failed to create GatewayServer");
                return nullptr;
            }
            return gateway_server;
        }
        private:
                std::shared_ptr<sw::redis::Redis> redis_client_;
            std::shared_ptr<ServiceManager> service_manager_;
            std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
            std::string user_service_name_;
            std::string file_service_name_;
            std::string speech_service_name_;
            std::string message_service_name_;
            std::string forward_service_name_;
            std::string friend_service_name_;
            int websocket_port_;
            int http_port_;
    };
            using GatewayServerFactory = GatewayServerBuilder;
}//namespace MicroChat