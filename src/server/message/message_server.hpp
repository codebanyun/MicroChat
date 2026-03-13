#pragma once
#include <brpc/server.h>
#include <butil/logging.h>
#include "es_manager.hpp"
#include "mysql_message.hpp"
#include "channel.hpp"
#include "rabbitmq.hpp"
#include "etcd.hpp"
#include "spdlog.hpp"
#include "utils.hpp"
#include "message.pb.h"
#include "base.pb.h"
#include "user.pb.h"
#include "file.pb.h"

namespace MicroChat {
    class MessageServiceImpl : public MsgStorageService {
    public:
        MessageServiceImpl(
            const std::shared_ptr<elasticlient::Client> &es_client,
            const std::shared_ptr<odb::core::database> &mysql_client,
            const std::shared_ptr<ServiceManager> &service_manager,
            const std::string &user_service_name,
            const std::string &file_service_name)
            : es_manager_(std::make_shared<ESMessageManager>(es_client)),
              message_table_(std::make_shared<MessageTable>(mysql_client)),
              service_manager_(service_manager),
              user_service_name_(user_service_name),
              file_service_name_(file_service_name) {
                es_manager_->create_message_index();
            }
        ~MessageServiceImpl() override {}
        virtual void GetHistoryMsg(google::protobuf::RpcController* controller,
                       const ::MicroChat::GetHistoryMsgReq* request,
                       ::MicroChat::GetHistoryMsgRsp* response,
                       ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
            };
            //1. 提取会话ID，请求ID，消息起始和结束时间戳
            std::string chat_session_id = request->chat_session_id();
            std::string request_id = request->request_id();
            if (chat_session_id.empty()) {
                err_response(request_id, "会话ID不能为空");
                LOG_WARN("获取历史消息失败，会话ID不能为空");
                return;
            }
            boost::posix_time::ptime begin_ts = boost::posix_time::from_time_t(request->start_time());
            boost::posix_time::ptime end_ts = boost::posix_time::from_time_t(request->over_time());
            //2. 从mysql数据库中获取历史消息
            std::vector<Message> messages = message_table_->rangeMessages(
                chat_session_id, begin_ts, end_ts);
            if(messages.empty()) {
                LOG_DEBUG("获取历史消息无结果，会话ID：{}，起始时间：{}，结束时间：{}",
                    chat_session_id, boost::posix_time::to_simple_string(begin_ts),
                    boost::posix_time::to_simple_string(end_ts));
                response->set_request_id(request_id);
                response->set_success(true);
                return;
            }
            LOG_DEBUG("获取历史消息成功，会话ID：{}，起始时间：{}，结束时间：{}，消息数量：{}",
                chat_session_id, boost::posix_time::to_simple_string(begin_ts),
                boost::posix_time::to_simple_string(end_ts), messages.size());
            //3. 获取消息类型和内容，从文件服务下载文件
            std::unordered_set<std::string> file_id_set;;
            for (const auto &msg : messages) {
                if (!msg.getFileId().empty()) {
                    LOG_DEBUG("消息包含文件，文件ID：{}", msg.getFileId());
                    file_id_set.insert(msg.getFileId());
                }
            }
            //调用文件服务下载文件信息
            std::unordered_map<std::string, std::string> file_info_map;
            //封装获取文件的请求
            bool ret = _Getfiles(request_id, file_id_set, file_info_map);
            if(!ret) {
                err_response(request_id, "获取历史消息失败，调用文件服务下载文件信息失败");
                LOG_ERROR("获取历史消息失败，调用文件服务下载文件信息失败");
                return;
            }
            //4. 获取消息发送者信息，调用用户服务获取用户信息
            std::unordered_map<std::string, UserInfo> user_info_map;
            std::unordered_set<std::string> user_id_lists;
            for (const auto &msg : messages) {
                user_id_lists.insert(msg.getUserId());
            }
            ret = _GetUsers(request_id, user_id_lists, user_info_map);
            if(!ret) {
                err_response(request_id, "获取历史消息失败，调用用户服务获取用户信息失败");
                LOG_ERROR("获取历史消息失败，调用用户服务获取用户信息失败");
                return;
            }
            //5. 组织响应消息列表，设置响应结果
            response->set_request_id(request_id);
            response->set_success(true);
            for (const auto &msg : messages) {
                MessageInfo *msg_info = response->add_msg_list();
                msg_info->set_message_id(msg.getMessageId());
                msg_info->set_chat_session_id(msg.getSessionId());
                msg_info->set_timestamp(boost::posix_time::to_time_t(msg.getTimestamp()));
                //设置消息发送者信息
                msg_info->mutable_sender()->CopyFrom(user_info_map[msg.getUserId()]);
                //设置消息内容
                switch (msg.getMessageType()) {
                    case MessageType::STRING: //文本消息
                        msg_info->mutable_message()->set_message_type(MessageType::STRING);
                        msg_info->mutable_message()->mutable_string_message()->set_content(msg.getContent());
                        break;
                    case MessageType::IMAGE: //图片消息
                        msg_info->mutable_message()->set_message_type(MessageType::IMAGE);
                        msg_info->mutable_message()->mutable_image_message()->set_file_id(msg.getFileId());
                        msg_info->mutable_message()->mutable_image_message()->set_image_content(file_info_map[msg.getFileId()]);
                        break;
                    case MessageType::FILE: //文件消息
                        msg_info->mutable_message()->set_message_type(MessageType::FILE);
                        msg_info->mutable_message()->mutable_file_message()->set_file_id(msg.getFileId());
                        msg_info->mutable_message()->mutable_file_message()->set_file_size(msg.getFileSize());
                        msg_info->mutable_message()->mutable_file_message()->set_file_name(msg.getFileName());
                        msg_info->mutable_message()->mutable_file_message()->set_file_contents(file_info_map[msg.getFileId()]);
                        break;
                    case MessageType::SPEECH: //语音消息
                        msg_info->mutable_message()->set_message_type(MessageType::SPEECH);
                        msg_info->mutable_message()->mutable_speech_message()->set_file_id(msg.getFileId());
                        msg_info->mutable_message()->mutable_speech_message()->set_file_contents(file_info_map[msg.getFileId()]);
                        break;
                    default:
                        LOG_WARN("未知的消息类型，消息ID：{}，消息类型：{}", msg.getMessageId(), msg.getMessageType());
                        return;
                }
            }
        }
        virtual void GetRecentMsg(google::protobuf::RpcController* controller,
                            const ::MicroChat::GetRecentMsgReq* request,
                            ::MicroChat::GetRecentMsgRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
            };
            //1. 提取会话ID，请求ID等
            std::string chat_session_id = request->chat_session_id();
            std::string request_id = request->request_id();
            if (chat_session_id.empty()) {
                err_response(request_id, "会话ID不能为空");
                LOG_WARN("获取最近消息失败，会话ID不能为空");
                return;
            }
            size_t msg_count = request->msg_count();
            //2. 从mysql数据库中获取最近消息
            std::vector<Message> messages = message_table_->recentNMessages(chat_session_id, msg_count);
            if(messages.empty()) {
                LOG_DEBUG("获取最近消息无结果，会话ID：{}，消息数量：{}",
                    chat_session_id, msg_count);
                response->set_request_id(request_id);
                response->set_success(true);
                return;
            }
            LOG_DEBUG("获取最近消息成功，会话ID：{}，消息数量：{}，实际获取数量：{}",
                chat_session_id, msg_count, messages.size());
            //3. 获取消息类型和内容，从文件服务下载文件
            std::unordered_set<std::string> file_id_set;;
            for (const auto &msg : messages) {
                if (!msg.getFileId().empty()) {
                    LOG_DEBUG("消息包含文件，文件ID：{}", msg.getFileId());
                    file_id_set.insert(msg.getFileId());
                }
            }
            //调用文件服务下载文件信息
            std::unordered_map<std::string, std::string> file_info_map;
            //封装获取文件的请求
            bool ret = _Getfiles(request_id, file_id_set, file_info_map);
            if(!ret) {
                err_response(request_id, "获取最近消息失败，调用文件服务下载文件信息失败");
                LOG_ERROR("获取最近消息失败，调用文件服务下载文件信息失败");
                return;
            }
            //4. 获取消息发送者信息，调用用户服务获取用户信息
            std::unordered_map<std::string, UserInfo> user_info_map;
            std::unordered_set<std::string> user_id_lists;
            for (const auto &msg : messages) {
                user_id_lists.insert(msg.getUserId());
            }
            ret = _GetUsers(request_id, user_id_lists, user_info_map);
            if(!ret) {
                err_response(request_id, "获取最近消息失败，调用用户服务获取用户信息失败");
                LOG_ERROR("获取最近消息失败，调用用户服务获取用户信息失败");
                return;
            }
            //5. 组织响应消息列表，设置响应结果
            response->set_request_id(request_id);
            response->set_success(true);
            for (const auto &msg : messages) {
                MessageInfo *msg_info = response->add_msg_list();
                msg_info->set_message_id(msg.getMessageId());
                msg_info->set_chat_session_id(msg.getSessionId());
                msg_info->set_timestamp(boost::posix_time::to_time_t(msg.getTimestamp()));
                //设置消息发送者信息
                msg_info->mutable_sender()->CopyFrom(user_info_map[msg.getUserId()]);
                //设置消息内容
                switch (msg.getMessageType()) {
                    case MessageType::STRING: //文本消息
                        msg_info->mutable_message()->set_message_type(MessageType::STRING);
                        msg_info->mutable_message()->mutable_string_message()->set_content(msg.getContent());
                        break;
                    case MessageType::IMAGE: //图片消息
                        msg_info->mutable_message()->set_message_type(MessageType::IMAGE);
                        msg_info->mutable_message()->mutable_image_message()->set_file_id(msg.getFileId());
                        msg_info->mutable_message()->mutable_image_message()->set_image_content(file_info_map[msg.getFileId()]);
                        break;
                    case MessageType::FILE: //文件消息
                        msg_info->mutable_message()->set_message_type(MessageType::FILE);
                        msg_info->mutable_message()->mutable_file_message()->set_file_id(msg.getFileId());
                        msg_info->mutable_message()->mutable_file_message()->set_file_size(msg.getFileSize());
                        msg_info->mutable_message()->mutable_file_message()->set_file_name(msg.getFileName());
                        msg_info->mutable_message()->mutable_file_message()->set_file_contents(file_info_map[msg.getFileId()]);
                        break;
                    case MessageType::SPEECH: //语音消息
                        msg_info->mutable_message()->set_message_type(MessageType::SPEECH);
                        msg_info->mutable_message()->mutable_speech_message()->set_file_id(msg.getFileId());
                        msg_info->mutable_message()->mutable_speech_message()->set_file_contents(file_info_map[msg.getFileId()]);
                        break;
                    default:
                        LOG_WARN("未知的消息类型，消息ID：{}，消息类型：{}", msg.getMessageId(), msg.getMessageType());
                        return;
                }
            }
        }
        virtual void MsgSearch(google::protobuf::RpcController* controller,
                            const ::MicroChat::MsgSearchReq* request,
                            ::MicroChat::MsgSearchRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
            };
            //1. 提取请求参数
            std::string chat_session_id = request->chat_session_id();
            std::string request_id = request->request_id();
            if (chat_session_id.empty()) {
                err_response(request_id, "会话ID不能为空");
                LOG_WARN("消息搜索失败，会话ID不能为空");
                return;
            }
            std::string search_key = request->search_key();
            if (search_key.empty()) {
                err_response(request_id, "搜索关键词不能为空");
                LOG_WARN("消息搜索失败，搜索关键词不能为空");
                return;
            }
            //2. 优先调用Elasticsearch进行消息搜索，失败时降级到MySQL
            bool es_ok = true;
            std::vector<Message> messages = es_manager_->search(chat_session_id, search_key, &es_ok);
            if (!es_ok) {
                LOG_WARN("ES搜索失败，降级到MySQL检索，会话ID：{}，搜索关键词：{}", chat_session_id, search_key);
                messages = message_table_->searchByKeyword(chat_session_id, search_key);
            }
            if(messages.empty()) {  
                LOG_DEBUG("消息搜索无结果，会话ID：{}，搜索关键词：{}", chat_session_id, search_key);
                response->set_request_id(request_id);
                response->set_success(true);
                return;
            }
            LOG_DEBUG("消息搜索成功，会话ID：{}，搜索关键词：{}，搜索结果数量：{}", chat_session_id, search_key, messages.size());
            //3. 获取消息发送者信息，调用用户服务获取用户信息
            std::unordered_map<std::string, UserInfo> user_info_map;
            std::unordered_set<std::string> user_id_lists;
            for (const auto &msg : messages) {
                user_id_lists.insert(msg.getUserId());
            }
            bool ret = _GetUsers(request_id, user_id_lists, user_info_map);
            if(!ret) {
                err_response(request_id, "消息搜索失败，调用用户服务获取用户信息失败");
                LOG_ERROR("消息搜索失败，调用用户服务获取用户信息失败");
                return; 
            }
            //4. 组织响应消息列表，设置响应结果
            response->set_request_id(request_id);
            response->set_success(true);
            for (const auto &msg : messages) {
                MessageInfo *msg_info = response->add_msg_list();
                msg_info->set_message_id(msg.getMessageId());
                msg_info->set_chat_session_id(msg.getSessionId());
                msg_info->set_timestamp(boost::posix_time::to_time_t(msg.getTimestamp()));
                //设置消息发送者信息
                msg_info->mutable_sender()->CopyFrom(user_info_map[msg.getUserId()]);
                //设置消息内容
                msg_info->mutable_message()->set_message_type(MessageType::STRING);
                msg_info->mutable_message()->mutable_string_message()->set_content(msg.getContent());
            }
        }
        //设置回调函数，用于消费异步消息并同步ES/文件服务
        bool message_callback(const char* body , size_t len) {
            //1. 解析消息内容，提取消息信息
            MessageInfo message;
            bool ret = message.ParseFromArray(body, len);
            if(!ret) {
                LOG_ERROR("解析RabbitMQ消息失败，消息内容：{}", std::string(body, len));
                return false;
            }
            //2. 根据消息类型做出不同处理
            std::string file_id, file_name, content;
            size_t file_size = 0;
            switch(message.message().message_type()) {
                case MessageType::STRING: //文本消息
                {
                    content = message.message().string_message().content();
                    ret = es_manager_->appendmessage(Message(message.message_id(), message.chat_session_id(),
                        message.sender().user_id(), message.message().message_type(), boost::posix_time::from_time_t(message.timestamp()), content, {}, {}, {}));
                    if(!ret) {
                        LOG_ERROR("向Elasticsearch索引 message 添加消息信息失败，消息ID：{}", message.message_id());
                        return false;
                    } else {
                        LOG_DEBUG("向Elasticsearch索引 message 添加消息信息成功，消息ID：{}", message.message_id());
                    }
                    break;
                }
                case MessageType::IMAGE: //图片消息
                {
                    const auto &msg = message.message().image_message();
                    ret = _PutFile(message.message_id(), "", msg.image_content(), msg.image_content().size(), file_id);
                    if(!ret) {
                        LOG_ERROR("调用文件服务上传图片失败，消息ID：{}", message.message_id());
                        return false;
                    } else {
                        LOG_DEBUG("调用文件服务上传图片成功，消息ID：{}，文件ID：{}", message.message_id(), file_id);
                    }
                    ret = message_table_->updateFileInfo(message.message_id(), file_id, "", 0);
                    if (!ret) {
                        LOG_ERROR("更新MySQL图片文件信息失败，消息ID：{}，文件ID：{}", message.message_id(), file_id);
                        return false;
                    }
                    break;
                }
                case MessageType::FILE: //文件消息
                {
                    const auto &msg = message.message().file_message();
                    file_name = msg.file_name();
                    file_size = msg.file_size();
                    ret = _PutFile(message.message_id(), file_name, msg.file_contents(), msg.file_contents().size(), file_id);
                    if(!ret) {
                        LOG_ERROR("调用文件服务上传文件失败，消息ID：{}", message.message_id());
                        return false;
                    } else {
                        LOG_DEBUG("调用文件服务上传文件成功，消息ID：{}，文件ID：{}", message.message_id(), file_id);
                    }
                    ret = message_table_->updateFileInfo(message.message_id(), file_id, file_name, static_cast<unsigned int>(file_size));
                    if (!ret) {
                        LOG_ERROR("更新MySQL文件信息失败，消息ID：{}，文件ID：{}", message.message_id(), file_id);
                        return false;
                    }
                    break;
                }
                case MessageType::SPEECH: //语音消息
                {
                    const auto &msg = message.message().speech_message();
                    ret = _PutFile(message.message_id(), "", msg.file_contents(), msg.file_contents().size(), file_id);
                    if(!ret) {
                        LOG_ERROR("调用文件服务上传语音失败，消息ID：{}", message.message_id());
                        return false;
                    } else {
                        LOG_DEBUG("调用文件服务上传语音成功，消息ID：{}，文件ID：{}", message.message_id(), file_id);
                    }
                    ret = message_table_->updateFileInfo(message.message_id(), file_id, "", 0);
                    if (!ret) {
                        LOG_ERROR("更新MySQL语音文件信息失败，消息ID：{}，文件ID：{}", message.message_id(), file_id);
                        return false;
                    }
                    break;
                }
                default:
                    LOG_WARN("未知的消息类型，消息ID：{}，消息类型：{}", message.message_id(), message.message().message_type());
                    return false;
            }
            return true;
        }
    private:
        //调用用户服务获取用户信息
        bool _GetUsers(const std::string &request_id,
                       const std::unordered_set<std::string> &user_id_lists,
                       std::unordered_map<std::string, UserInfo> &user_info_map) {
            auto channel = service_manager_ -> get_service_node(user_service_name_);
            if (!channel) {
                LOG_ERROR("获取用户服务信道失败，无法调用用户服务获取用户信息");
                return false;
            }
            UserService_Stub user_stub(channel.get());
            GetMultiUserInfoReq user_req;
            GetMultiUserInfoRsp user_rsp;
            user_req.set_request_id(request_id);
            for (const auto &user_id : user_id_lists) {
                user_req.add_users_id(user_id);
            }
            brpc::Controller cntl;
            user_stub.GetMultiUserInfo(&cntl, &user_req, &user_rsp, nullptr);
            if (cntl.Failed() || !user_rsp.success()) {
                LOG_ERROR("调用用户服务获取用户信息失败，错误码：{}，错误信息：{}", cntl.ErrorCode(), cntl.ErrorText());
                return false;
            }
            for (const auto &user_info : user_rsp.users_info()) {
                user_info_map[user_info.first] = user_info.second;
            }
            return true;
        }
                        
        //调用文件服务获取文件信息
        bool _Getfiles(const std::string &request_id,
                       const std::unordered_set<std::string> &file_id_set,
                       std::unordered_map<std::string, std::string> &file_info_map) {
            auto channel = service_manager_ -> get_service_node(file_service_name_);
            if (!channel) {
                LOG_ERROR("获取文件服务信道失败，无法调用文件服务获取文件信息");
                return false;
            }
            FileService_Stub file_stub(channel.get());
            GetMultiFileReq file_req;
            GetMultiFileRsp file_rsp;
            file_req.set_request_id(request_id);
            for (const auto &file_id : file_id_set) {
                file_req.add_file_id_list(file_id);
            }
            brpc::Controller cntl;
            file_stub.GetMultiFile(&cntl, &file_req, &file_rsp, nullptr);
            if (cntl.Failed() || !file_rsp.success()) {
                LOG_ERROR("调用文件服务获取文件信息失败，错误码：{}，错误信息：{}", cntl.ErrorCode(), cntl.ErrorText());
                return false;
            }
            for (const auto &file_info : file_rsp.file_data()) {
                file_info_map[file_info.first] = file_info.second.file_content();
            }
            return true;
        }
        //调用文件服务上传文件
        bool _PutFile(const std::string &request_id, const std::string &file_name, const std::string& file_content, size_t file_size, std::string &file_id) {
            auto channel = service_manager_ -> get_service_node(file_service_name_);
            if (!channel) {
                LOG_ERROR("获取文件服务信道失败，无法调用文件服务上传文件");
                return false;
            }
            FileService_Stub file_stub(channel.get());
            PutSingleFileReq file_req;
            PutSingleFileRsp file_rsp;
            file_req.set_request_id(request_id);
            file_req.mutable_file_data()->set_file_name(file_name);
            file_req.mutable_file_data()->set_file_size(file_size);
            file_req.mutable_file_data()->set_file_content(file_content);
            brpc::Controller cntl;
            file_stub.PutSingleFile(&cntl, &file_req, &file_rsp, nullptr);
            if (cntl.Failed() || !file_rsp.success()) {
                LOG_ERROR("调用文件服务上传文件失败，错误码：{}，错误信息：{}", cntl.ErrorCode(), cntl.ErrorText());
                return false;
            }
            file_id = file_rsp.file_info().file_id();
            return true;
        }
    private:
        std::string user_service_name_;//用户服务名称
        std::string file_service_name_;//文件服务名称
        std::shared_ptr<ESMessageManager> es_manager_; // Elasticsearch管理器
        std::shared_ptr<MessageTable> message_table_; // mysql数据库客户端
        std::shared_ptr<ServiceManager> service_manager_; //服务信道管理器
    };
    class MessageServer {
    public:
        MessageServer(const std::shared_ptr<elasticlient::Client> es_client,
                      const std::shared_ptr<odb::core::database> &mysql_client,
                      const std::shared_ptr<RabbitMQClient> &rabbitmq_client,
                      const std::shared_ptr<EtcdClientfinder> etcd_client_finder,
                      const std::shared_ptr<EtcdClientRegistry> etcd_client_registry,
                      const std::shared_ptr<brpc::Server> brpc_server)
            : es_client_(es_client),
              mysql_client_(mysql_client),
              rabbitmq_client_(rabbitmq_client),
              etcd_client_finder_(etcd_client_finder),
              etcd_client_registry_(etcd_client_registry),
              brpc_server_(brpc_server) {}
        void start() {
            brpc_server_ ->RunUntilAskedToQuit();
        }
    private:         
        std::shared_ptr<elasticlient::Client> es_client_;
        std::shared_ptr<odb::core::database> mysql_client_;
        std::shared_ptr<RabbitMQClient> rabbitmq_client_;
        std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
        std::shared_ptr<EtcdClientRegistry> etcd_client_registry_;
        std::shared_ptr<brpc::Server> brpc_server_;
    };
    class MessageServerFactory {
    public:
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
        //构造ES客户端
        bool create_es_client(const std::vector<std::string> &hosts) {
            es_client_ = ESClientBuilder::createClient(hosts);
            return es_client_ != nullptr;
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
            const std::string& user_service_name,
            const std::string& file_service_name) {
            user_service_name_ = user_service_name;
            file_service_name_ = file_service_name;
            service_manager_ = std::make_shared<ServiceManager>();
            if (!service_manager_) {
                LOG_ERROR("Failed to create ServiceManager");
                return false;
            }
            service_manager_->add_service_name(user_service_name_);
            service_manager_->add_service_name(file_service_name_);
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
        // 构造RabbitMQ客户端
        bool create_rabbitmq_client(
            const std::string &user, 
            const std::string &passwd,
            const std::string &host,
            const std::string &exchange_name,
            const std::string &queue_name,
            const std::string &binding_key,
            const std::string &retry_exchange_name,
            const std::string &retry_queue_name,
            const std::string &retry_binding_key,
            const std::string &dead_exchange_name,
            const std::string &dead_queue_name,
            const std::string &dead_binding_key,
            uint32_t retry_delay_ms,
            uint32_t max_retry_count) {
            exchange_name_ = exchange_name;
            queue_name_ = queue_name;
            rabbitmq_client_ = std::make_shared<RabbitMQClient>(user, passwd, host);
            if (!rabbitmq_client_ ) {
                LOG_ERROR("Failed to create RabbitMQClient");
                return false;
            }
            rabbitmq_client_->declareComponentsWithRetry(
                exchange_name,
                queue_name,
                binding_key,
                retry_exchange_name,
                retry_queue_name,
                retry_binding_key,
                dead_exchange_name,
                dead_queue_name,
                dead_binding_key,
                retry_delay_ms,
                max_retry_count);
            return true;
        }
        // 构造rpc服务器
        bool create_brpc_server(uint16_t port , int timeout , int thread_num) {
            if(!es_client_ || !mysql_client_ || !service_manager_ || !rabbitmq_client_) {
                LOG_ERROR("Failed to create brpc server, dependencies are not satisfied");
                return false;
            }
            brpc_server_ = std::make_shared<brpc::Server>();
            MessageServiceImpl* message_service_impl = new MessageServiceImpl(
                es_client_,
                mysql_client_,
                service_manager_,
                user_service_name_,
                file_service_name_);
            if (brpc_server_->AddService(message_service_impl, brpc::SERVER_OWNS_SERVICE) != 0) {
                LOG_ERROR("Failed to add MessageService to brpc server");
                return false;
            }
            brpc::ServerOptions options;
            options.idle_timeout_sec = timeout;
            options.num_threads = thread_num;
            if (brpc_server_->Start(port, &options) != 0) {
                LOG_ERROR("Failed to start brpc server on port {}", port);
                return false;
            }
            rabbitmq_client_->consumeMessage(queue_name_, [message_service_impl](const char* body, size_t len) {
                return message_service_impl->message_callback(body, len);
            });
            return true;
        }
        //构造消息服务器
        std::shared_ptr<MessageServer> create_message_server() {
            if(!es_client_ || !mysql_client_ || !rabbitmq_client_ ||
               !etcd_client_finder_ || !etcd_client_registry_ || !brpc_server_) {
                LOG_ERROR("Failed to create MessageServer, dependencies are not satisfied");
                return nullptr;
            }
            return std::make_shared<MessageServer>(
                es_client_,
                mysql_client_,
                rabbitmq_client_,
                etcd_client_finder_,
                etcd_client_registry_,
                brpc_server_);
        }
    private:
        std::string user_service_name_;
        std::string file_service_name_;
        std::string exchange_name_;
        std::string queue_name_;
        std::shared_ptr<elasticlient::Client> es_client_;
        std::shared_ptr<odb::core::database> mysql_client_;
        std::shared_ptr<RabbitMQClient> rabbitmq_client_;
        std::shared_ptr<ServiceManager> service_manager_;
        std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
        std::shared_ptr<EtcdClientRegistry> etcd_client_registry_;
        std::shared_ptr<brpc::Server> brpc_server_;
    };
}// namespace MicroChat