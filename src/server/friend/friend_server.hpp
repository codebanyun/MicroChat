#pragma once
#include <brpc/server.h>
#include <butil/logging.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include "es_manager.hpp"
#include "channel.hpp"
#include "rabbitmq.hpp"
#include "etcd.hpp"
#include "spdlog.hpp"
#include "utils.hpp"
#include "mysql_chat_session.hpp"
#include "mysql_chat_session_member.hpp"
#include "mysql_friend_request.hpp"
#include "mysql_relation.hpp"
#include "friend.pb.h"
#include "base.pb.h"
#include "user.pb.h"
#include "message.pb.h"

namespace MicroChat{
    class FriendServiceImpl : public FriendService {
    public:
        FriendServiceImpl(
            const std::shared_ptr<elasticlient::Client> &es_client,
            const std::shared_ptr<odb::core::database> &mysql_client,
            const std::shared_ptr<ServiceManager> &service_manager,
            const std::string &user_service_name,
            const std::string &message_service_name)
            : relation_table_(std::make_shared<RelationTable>(mysql_client)),
              chat_session_table_(std::make_shared<ChatSessionTable>(mysql_client)),
              chat_session_member_table_(std::make_shared<ChatSessionMemberTable>(mysql_client)),
              friend_request_table_(std::make_shared<FriendRequestTable>(mysql_client)),
              es_manager_(std::make_shared<ESUserManager>(es_client)),
              service_manager_(service_manager),
              user_service_name_(user_service_name),
              message_service_name_(message_service_name) {}
        ~FriendServiceImpl() override {}
        virtual void GetFriendList(::google::protobuf::RpcController* controller,
                       const ::MicroChat::GetFriendListReq* request,
                       ::MicroChat::GetFriendListRsp* response,
                       ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //1. 错误回调
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg) -> void {
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //2. 从请求中获取用户ID
            const std::string &rid = request->request_id();
            const std::string &user_id = request->user_id();
            //3. 获取好友列表
            std::vector<std::string> friend_id_list = relation_table_->getFriends(user_id);
            //4. 从用户服务获取好友信息
            std::unordered_map<std::string, UserInfo> friend_info_map;
            std::unordered_set<std::string> friend_id_set(friend_id_list.begin(), friend_id_list.end());
            bool ret = GetUserInfo(rid, friend_id_set, friend_info_map);
            if (!ret) {
                err_response(rid, "获取好友信息失败，调用用户服务失败");
                return;
            }
            //5. 构造响应
            response->set_request_id(rid);
            response->set_success(true);
            for(const auto &friend_info : friend_info_map) {
                response->add_friend_list()->CopyFrom(friend_info.second);
            }
            LOG_INFO("获取好友列表成功，user_id: {}, friend_count: {}", user_id, friend_info_map.size());
        }
        virtual void FriendRemove(::google::protobuf::RpcController* controller,
                            const ::MicroChat::FriendRemoveReq* request,
                            ::MicroChat::FriendRemoveRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //1. 从请求中获取用户ID和好友ID
            const std::string &rid = request->request_id();
            const std::string &user_id = request->user_id();
            const std::string &friend_id = request->friend_id();
            if (user_id.empty() || friend_id.empty()) {
                err_response(rid, "用户ID和好友ID不能为空");
                LOG_WARN("删除好友失败，用户ID或好友ID为空，user_id: {}, friend_id: {}", user_id, friend_id);
                return;
            }
            //2. 删除好友关系
            bool ret = relation_table_->del_relation(user_id, friend_id);
            if (!ret) {                
                err_response(rid, "删除好友关系失败");
                LOG_ERROR("删除好友关系失败，user_id: {}, friend_id: {}", user_id, friend_id);
                return;
            }
            //3. 删除相关的单聊会话
            ret = chat_session_table_->delSingleChatSession(user_id, friend_id);
            if (!ret) {
                err_response(rid, "删除单聊会话失败");
                LOG_ERROR("删除单聊会话失败，user_id: {}, friend_id: {}", user_id, friend_id);
                return;
            }
            //4. 组织响应
            response->set_request_id(rid);
            response->set_success(true);
            LOG_INFO("删除好友成功，user_id: {}, friend_id: {}", user_id, friend_id);
        }
        virtual void FriendAdd(::google::protobuf::RpcController* controller,
                            const ::MicroChat::FriendAddReq* request,
                            ::MicroChat::FriendAddRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //1. 从请求中获取用户ID、好友ID和申请消息
            const std::string &rid = request->request_id();
            const std::string &user_id = request->user_id();
            const std::string &recipient_id = request->recipient_id();
            const std::string &message = request->message();
            if (user_id.empty() || recipient_id.empty()) {
                err_response(rid, "用户ID和被申请者ID不能为空");
                LOG_WARN("添加好友失败，用户ID或被申请者ID为空，user_id: {}, recipient_id: {}", user_id, recipient_id);
                return;
            }
            //2. 判断是否已经是好友关系
            bool is_friend = relation_table_->isFriend(user_id, recipient_id);
            if (is_friend) {
                err_response(rid, "已经是好友关系");
                LOG_WARN("添加好友失败，已经是好友关系，user_id: {}, recipient_id: {}", user_id, recipient_id);
                return;
            }
            //3. 判断是否存在未处理的好友请求
            bool exists_request = friend_request_table_->exists_pending_request(user_id, recipient_id);
            if (exists_request) {
                err_response(rid, "已经向对方发送过好友请求");
                LOG_WARN("添加好友失败，已经向对方发送过好友请求，user_id: {}, recipient_id: {}", user_id, recipient_id);
                return;
            }
            //4. 插入好友请求记录
            std::string event_id = UUID();
            auto friend_request = std::make_shared<FriendRequest>(event_id, user_id, recipient_id, boost::posix_time::second_clock::universal_time(), message);
            bool ret = friend_request_table_->insert(friend_request);
            if (!ret) {
                err_response(rid, "发送好友请求失败，数据库操作失败");
                LOG_ERROR("发送好友请求失败，数据库操作失败，user_id: {}, recipient_id: {}", user_id, recipient_id);
                return;
            }
            //5. 组织响应
            response->set_request_id(rid);
            response->set_success(true);
            response->set_notify_event_id(event_id);
            LOG_INFO("发送好友请求成功，user_id: {}, recipient_id: {}", user_id, recipient_id);
        }
        virtual void FriendAddProcess(::google::protobuf::RpcController* controller,
                            const ::MicroChat::FriendAddProcessReq* request,
                            ::MicroChat::FriendAddProcessRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //1. 从请求中获取用户ID、被申请人ID、处理结果和事件ID
            const std::string &rid = request->request_id();
            const std::string &sender_id = request->sender_id();
            const std::string &recipient_id = request->recipient_id();
            const std::string &event_id = request->notify_event_id();
            bool accept = request->agree();
            if (sender_id.empty() || recipient_id.empty() || event_id.empty()) {
                err_response(rid, "用户ID、申请人ID和事件ID不能为空");
                LOG_WARN("处理好友请求失败，申请人ID、被申请人ID或事件ID为空，sender_id: {}, recipient_id: {}, event_id: {}", sender_id, recipient_id, event_id);
                return;
            }
            //2. 获取好友请求记录，判断请求是否存在
            bool exists_request = friend_request_table_->exists_pending_request(sender_id, recipient_id);
            if (!exists_request) {
                err_response(rid, "好友请求不存在或已被处理");
                LOG_WARN("处理好友请求失败，好友请求不存在或已被处理，sender_id: {}, recipient_id: {}", sender_id, recipient_id);
                return;
            }
            //3. 如果同意好友请求，插入好友关系记录，创建单聊会话
            std::string ssid = UUID();
            if (accept) {
                bool ret = relation_table_->insert(std::make_shared<Relation>(sender_id, recipient_id));
                if (!ret) {
                    err_response(rid, "同意好友请求失败，数据库操作失败");
                    LOG_ERROR("同意好友请求失败，数据库操作失败，sender_id: {}, recipient_id: {}", sender_id, recipient_id);
                    return;
                }
                ret = chat_session_table_->insert(std::make_shared<ChatSession>(ssid, "" ,ChatSessionType::SINGLE));
                if (!ret) {                    
                    err_response(rid, "同意好友请求失败，创建单聊会话失败");
                    LOG_ERROR("同意好友请求失败，创建单聊会话失败，sender_id: {}, recipient_id: {}", sender_id, recipient_id);
                    return;
                }
                std::vector<std::shared_ptr<ChatSessionMember>> members = {
                    std::make_shared<ChatSessionMember>(ssid, sender_id),
                    std::make_shared<ChatSessionMember>(ssid, recipient_id)
                };
                ret = chat_session_member_table_->insert(members);
                if (!ret) {
                    err_response(rid, "同意好友请求失败，创建单聊会话成员关系失败");
                    LOG_ERROR("同意好友请求失败，创建单聊会话成员关系失败，sender_id: {}, recipient_id: {}", sender_id, recipient_id);
                    return;
                }
            }
            //4. 删除好友请求记录
            bool ret = friend_request_table_->del(sender_id, recipient_id);
            if (!ret) {
                err_response(rid, "处理好友请求失败，删除好友请求记录失败");
                LOG_ERROR("处理好友请求失败，删除好友请求记录失败，sender_id: {}, recipient_id: {}", sender_id, recipient_id);
                return;
            }
            //5. 组织响应
            response->set_request_id(rid);
            response->set_success(true);
            response->set_new_session_id(accept ? ssid : "");
            LOG_INFO("处理好友请求成功，sender_id: {}, recipient_id: {}, accept: {}", sender_id, recipient_id, accept);
        }
        virtual void FriendSearch(::google::protobuf::RpcController* controller,
                            const ::MicroChat::FriendSearchReq* request,
                            ::MicroChat::FriendSearchRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){ 
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //1. 从请求中获取用户ID和搜索关键词
            const std::string &rid = request->request_id();
            const std::string &user_id = request->user_id();
            const std::string &keyword = request->search_key();
            if (user_id.empty()) {
                err_response(rid, "用户ID不能为空");
                LOG_WARN("好友搜索失败，用户ID为空");
                return;
            }
            if (keyword.empty()) {
                err_response(rid, "搜索关键词不能为空");
                LOG_WARN("好友搜索失败，搜索关键词为空");
                return;
            }
            //2. 根据用户ID，获取用户的好友ID列表
            std::vector<std::string> friend_id_list = relation_table_->getFriends(user_id);
            //3. 从好友ID列表中筛选出符合搜索关键词的好友ID列表
            friend_id_list.push_back(user_id); //过滤自己和好友列表
            std::vector<User> search_user_list = es_manager_->search(keyword, friend_id_list);
            std::vector<std::string> search_id_list;
            search_id_list.reserve(search_user_list.size());
            for (const auto &u : search_user_list) {
                search_id_list.push_back(u.getUserId());
            }
            if (search_id_list.empty()) {
                err_response(rid, "没有搜索到符合条件的好友");
                LOG_DEBUG("好友搜索无结果，user_id: {}, keyword: {}", user_id, keyword);
                return;
            }
            std::unordered_set<std::string> search_id_set(search_id_list.begin(), search_id_list.end());
            //4. 从用户服务获取搜索结果的用户信息
            std::unordered_map<std::string, UserInfo> user_info_map;
            bool ret = GetUserInfo(rid, search_id_set, user_info_map);
            if (!ret) {
                err_response(rid, "好友搜索失败，调用用户服务获取用户信息失败");
                LOG_ERROR("好友搜索失败，调用用户服务获取用户信息失败，user_id: {}, keyword: {}", user_id, keyword);
                return;
            }
            //5. 组织响应
            response->set_request_id(rid);
            response->set_success(true);
            for (const auto &user_info : user_info_map) {
                response->add_user_info()->CopyFrom(user_info.second);
            }
            LOG_INFO("好友搜索成功，user_id: {}, keyword: {}, search_count: {}", user_id, keyword, user_info_map.size());
        }
        virtual void GetChatSessionList(::google::protobuf::RpcController* controller,
                            const ::MicroChat::GetChatSessionListReq* request,
                            ::MicroChat::GetChatSessionListRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //1. 从请求中获取用户ID
            const std::string &rid = request->request_id();
            const std::string &user_id = request->user_id();
            if (user_id.empty()) {
                err_response(rid, "用户ID不能为空");
                LOG_WARN("获取聊天会话列表失败，用户ID为空");
                return;
            }
            //2. 获取用户的单聊会话列表
            std::vector<SingleChatSession> single_chat_session_list = chat_session_table_->getSingleChatSessions(user_id);
            //3. 获取用户的群聊会话列表
            std::vector<GroupChatSession> group_chat_session_list = chat_session_table_->getGroupChatSessions(user_id);
            if (single_chat_session_list.empty() && group_chat_session_list.empty()) {
                response->set_request_id(rid);
                response->set_success(true);
                LOG_DEBUG("获取聊天会话列表为空，user_id: {}", user_id);
                return;
            }
            //2.1 从单聊会话列表中提取好友ID列表，从用户服务获取好友信息
            std::unordered_map<std::string, UserInfo> friend_info_map;
            if (!single_chat_session_list.empty()) {
                std::unordered_set<std::string> friend_id_list;
                for(const auto &session : single_chat_session_list) {
                    friend_id_list.insert(session.friend_id);
                }
                bool ret = GetUserInfo(rid, friend_id_list, friend_info_map);
                if (!ret) {
                    err_response(rid, "获取聊天会话列表失败，调用用户服务获取好友信息失败");
                    LOG_ERROR("获取聊天会话列表失败，调用用户服务获取好友信息失败，user_id: {}", user_id);
                    return;
                }
            }
            //4. 根据所有的会话ID，从消息存储子服务获取会话最后一条消息
            //5. 组织响应，会话名称就是好友名称；会话头像就是好友头像
            for (const auto &session : single_chat_session_list) {
                ChatSessionInfo *session_info = response->add_chat_session_info_list();
                session_info->set_chat_session_id(session.chat_session_id);
                session_info->set_single_chat_friend_id(session.friend_id);
                session_info->set_chat_session_name(friend_info_map[session.friend_id].nickname());
                session_info->set_avatar(friend_info_map[session.friend_id].avatar());
                MessageInfo msg;
                bool ret = GetRecentMsg(rid, session.chat_session_id, msg);
                if (ret == false) continue;
                session_info->mutable_prev_message()->CopyFrom(msg);
            }
            for(const auto &session : group_chat_session_list) {
                ChatSessionInfo *session_info = response->add_chat_session_info_list();
                session_info->set_chat_session_id(session.chat_session_id);
                session_info->set_chat_session_name(session.chat_session_name);
                MessageInfo msg;
                bool ret = GetRecentMsg(rid, session.chat_session_id, msg);
                if (ret == false) continue;
                session_info->mutable_prev_message()->CopyFrom(msg);
            }
            response->set_request_id(rid);
            response->set_success(true);
            LOG_INFO("获取聊天会话列表成功，user_id: {}, single_chat_count: {}, group_chat_count: {}", user_id, single_chat_session_list.size(), group_chat_session_list.size());      
        }
        virtual void ChatSessionCreate(::google::protobuf::RpcController* controller,
                            const ::MicroChat::ChatSessionCreateReq* request,
                            ::MicroChat::ChatSessionCreateRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //1. 从请求中获取用户ID、会话类型、会话名称和成员ID列表
            const std::string &rid = request->request_id();
            const std::string &creator_id = request->user_id();
            const std::string &session_name = request->chat_session_name();
            std::vector<std::string> member_id_list(request->member_id_list().begin(), request->member_id_list().end());
            if (creator_id.empty()) {
                err_response(rid, "用户ID不能为空");
                LOG_WARN("创建聊天会话失败，用户ID为空");
                return;
            }
            //2.向数据库添加会话信息，添加会话成员信息
            std::string ssid = UUID();
            bool ret = chat_session_table_->insert(std::make_shared<ChatSession>(ssid, session_name, ChatSessionType::GROUP));
            if (!ret) {
                err_response(rid, "创建聊天会话失败，数据库操作失败");
                LOG_ERROR("创建聊天会话失败，数据库操作失败，creator_id: {}, session_name: {}", creator_id, session_name);
                return;
            }
            //添加会话成员信息
            for(const auto &member_id : member_id_list) {
                ret = chat_session_member_table_->insert(std::make_shared<ChatSessionMember>(ssid, member_id));
                if (!ret) {
                    err_response(rid, "创建聊天会话失败，数据库操作失败");
                    LOG_ERROR("创建聊天会话失败，数据库操作失败，creator_id: {}, session_name: {}, member_id: {}", creator_id, session_name, member_id);
                    return;
                }
            }
            response->set_request_id(rid);
            response->set_success(true);
            response->mutable_chat_session_info()->set_chat_session_id(ssid);
            response->mutable_chat_session_info()->set_chat_session_name(session_name);
            LOG_INFO("创建聊天会话成功，creator_id: {}, session_name: {}, session_id: {}", creator_id, session_name, ssid);
        }
        virtual void GetChatSessionMember(::google::protobuf::RpcController* controller,
                            const ::MicroChat::GetChatSessionMemberReq* request,
                            ::MicroChat::GetChatSessionMemberRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //1. 从请求中获取会话ID
            const std::string &rid = request->request_id();
            const std::string &user_id = request->user_id();
            const std::string &chat_session_id = request->chat_session_id();
            if (chat_session_id.empty()) {
                err_response(rid, "会话ID不能为空");
                LOG_WARN("获取聊天会话成员失败，会话ID为空");
                return;
            }
            //2. 获取会话成员ID列表
            std::vector<std::string> member_id_list = chat_session_member_table_->getMembers(chat_session_id);
            if (member_id_list.empty()) {
                err_response(rid, "没有会话成员");
                LOG_DEBUG("获取聊天会话成员无结果，chat_session_id: {}", chat_session_id);
                return;
            }
            //3. 从用户服务获取成员信息
            std::unordered_map<std::string, UserInfo> member_info_map;
            std::unordered_set<std::string> member_id_set(member_id_list.begin(), member_id_list.end());
            bool ret = GetUserInfo(rid, member_id_set, member_info_map);
            if (!ret) {
                err_response(rid, "获取聊天会话成员信息失败，调用用户服务失败");
                LOG_ERROR("获取聊天会话成员信息失败，调用用户服务失败，chat_session_id: {}", chat_session_id);
                return;
            }
            //4. 组织响应
            response->set_request_id(rid);
            response->set_success(true);    
            for (const auto &member_info : member_info_map) {
                response->add_member_info_list()->CopyFrom(member_info.second);
            }
            LOG_INFO("获取聊天会话成员成功，chat_session_id: {}, member_count: {}", chat_session_id, member_info_map.size());
        }
        virtual void GetPendingFriendEventList(::google::protobuf::RpcController* controller,
                            const ::MicroChat::GetPendingFriendEventListReq* request,
                            ::MicroChat::GetPendingFriendEventListRsp* response,
                            ::google::protobuf::Closure* done) {
            brpc::ClosureGuard done_guard(done);
            //错误处理函数
            auto err_response = [this, response](const std::string &rid, 
                const std::string &errmsg){
                response->set_request_id(rid);
                response->set_success(false);
                response->set_errmsg(errmsg);
                return;
            };
            //1. 从请求中获取用户ID
            const std::string &rid = request->request_id();
            const std::string &user_id = request->user_id();
            if (user_id.empty()) {
                err_response(rid, "用户ID不能为空");
                LOG_WARN("获取待处理好友事件列表失败，用户ID为空");
                return;
            }
            //2. 获取待处理的好友申请列表
            std::vector<FriendRequest> pending_lists = friend_request_table_->getPendingRequests(user_id);
            if (pending_lists.empty()) {
                err_response(rid, "没有待处理的好友事件");
                LOG_DEBUG("获取待处理好友事件列表无结果，user_id: {}", user_id);
                return;
            }
            //3. 从用户服务获取申请人用户信息
            std::unordered_map<std::string, UserInfo> user_info_map;
            std::unordered_set<std::string> related_user_id_set;
            for (const auto &req : pending_lists) {
                related_user_id_set.insert(req.getRequesterId());    
            }
            bool ret = GetUserInfo(rid, related_user_id_set, user_info_map);
            if (!ret) {
                err_response(rid, "获取待处理好友事件相关用户信息失败，调用用户服务失败");
                LOG_ERROR("获取待处理好友事件相关用户信息失败，调用用户服务失败， user_id: {}", user_id);
                return;
            }
            //4. 组织响应          
            response->set_request_id(rid);
            response->set_success(true);
            for (const auto &req : pending_lists) {
                FriendEvent *event_info = response->add_friend_event_info_list();
                event_info->set_event_id(req.getEventId());
                event_info->mutable_sender()->CopyFrom(user_info_map[req.getRequesterId()]);
                event_info->set_message(req.getMessage());
                //转换boost::posix_time::ptime为时间戳
                event_info->set_timestamp(boost::posix_time::to_time_t(req.getTimestamp()));
            }
            LOG_INFO("获取待处理好友事件列表成功，user_id: {}, event_count: {}", user_id, pending_lists.size());
        }
    private:
        bool GetUserInfo(std::string request_id, const std::unordered_set<std::string> &user_id_list, std::unordered_map<std::string, UserInfo> &user_info_map) {
            auto channel = service_manager_ -> get_service_node(user_service_name_);
            if (!channel) {
                LOG_ERROR("获取用户服务信道失败，无法调用用户服务获取用户信息");
                return false;
            }
            UserService_Stub user_stub(channel.get());
            GetMultiUserInfoReq user_req;
            GetMultiUserInfoRsp user_rsp;
            user_req.set_request_id(request_id);
            for (const auto &user_id : user_id_list) {
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
        bool GetRecentMsg(std::string request_id, const std::string &chat_session_id, MessageInfo &msg_info) {
            auto channel = service_manager_ -> get_service_node(message_service_name_);
            if (!channel) {
                LOG_ERROR("获取消息服务信道失败，无法调用消息服务获取最近消息");
                return false;
            }
            //封装请求
            GetRecentMsgReq request;
            request.set_request_id(request_id);
            request.set_chat_session_id(chat_session_id);
            request.set_msg_count(1);
            //调用消息服务获取最近消息
            GetRecentMsgRsp response;
            brpc::Controller cntl;
            MsgStorageService_Stub message_stub(channel.get());
            message_stub.GetRecentMsg(&cntl, &request, &response, nullptr);
            if (cntl.Failed() || !response.success()) {
                LOG_ERROR("调用消息服务获取最近消息失败，错误码：{}，错误信息：{}", cntl.ErrorCode(), cntl.ErrorText());
                return false;
            }
            if (response.msg_list_size() > 0) {
                msg_info.CopyFrom(response.msg_list(0));
            } else {
                LOG_DEBUG("没有最近消息，chat_session_id: {}", chat_session_id);
            }
            return true;
        }
    private:
        std::shared_ptr<RelationTable> relation_table_; //好友关系表
        std::shared_ptr<ChatSessionTable> chat_session_table_; //聊天会话表
        std::shared_ptr<ChatSessionMemberTable> chat_session_member_table_; //聊天会话成员表
        std::shared_ptr<FriendRequestTable> friend_request_table_; //好友请求表
        std::shared_ptr<ESUserManager> es_manager_; //ES用户信息管理器
        std::shared_ptr<ServiceManager> service_manager_; //服务信道管理器
        std::string user_service_name_;//用户服务名称
        std::string message_service_name_;//消息服务名称
    };
    class FriendServer {
    public:
        FriendServer(std::shared_ptr<EtcdClientfinder> etcd_client_finder,
                    std::shared_ptr<EtcdClientRegistry> etcd_client_registry,
                    std::shared_ptr<elasticlient::Client> es_client,
                    std::shared_ptr<odb::core::database> mysql_client,
                    const std::shared_ptr<brpc::Server> &server)
            : etcd_client_finder_(etcd_client_finder),
              etcd_client_registry_(etcd_client_registry),
              es_client_(es_client),
              mysql_client_(mysql_client),
              rpc_server_(server) {}
        ~FriendServer() = default;
        void start() {
            rpc_server_ -> RunUntilAskedToQuit();
        }
    private:
        std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
        std::shared_ptr<EtcdClientRegistry> etcd_client_registry_;
        std::shared_ptr<elasticlient::Client> es_client_;
        std::shared_ptr<odb::core::database> mysql_client_;
        std::shared_ptr<brpc::Server> rpc_server_;//brpc服务器
    };
    class FriendServerFactory {
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
            const std::string& message_service_name) {
            user_service_name_ = user_service_name;
            message_service_name_ = message_service_name;
            service_manager_ = std::make_shared<ServiceManager>();
            if (!service_manager_) {
                LOG_ERROR("Failed to create ServiceManager");
                return false;
            }
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
        //构造brpc服务器
        bool create_brpc_server(uint16_t port , int timeout , int thread_num) {
            if(!es_client_ || !mysql_client_ || !service_manager_) {
                LOG_ERROR("Failed to create brpc server, dependencies are not satisfied");
                return false;
            }
            brpc_server_ = std::make_shared<brpc::Server>();
            FriendServiceImpl* friend_service_impl = new FriendServiceImpl(
                es_client_,
                mysql_client_,
                service_manager_,
                user_service_name_,
                message_service_name_);
            if (brpc_server_->AddService(friend_service_impl, brpc::SERVER_OWNS_SERVICE) != 0) {
                LOG_ERROR("Failed to add FriendService to brpc server");
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
        //构造好友服务器
        std::shared_ptr<FriendServer> create_friend_server() {
            if(!etcd_client_finder_ || !etcd_client_registry_ || !es_client_ || !mysql_client_ || !brpc_server_) {
                LOG_ERROR("Failed to create FriendServer, dependencies are not satisfied");
                return nullptr;
            }
            return std::make_shared<FriendServer>(
                etcd_client_finder_,
                etcd_client_registry_,
                es_client_,
                mysql_client_,
                brpc_server_);
        }
    private:
        std::string user_service_name_;
        std::string message_service_name_;
        std::shared_ptr<elasticlient::Client> es_client_;
        std::shared_ptr<odb::core::database> mysql_client_;
        std::shared_ptr<ServiceManager> service_manager_;
        std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
        std::shared_ptr<EtcdClientRegistry> etcd_client_registry_;
        std::shared_ptr<brpc::Server> brpc_server_;
    };
}  //namespace MicroChat