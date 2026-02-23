#pragma once
#include "mysql.hpp"
#include "chat_session.hxx"
#include "chat_session-odb.hxx"
#include "mysql_chat_session_member.hpp"

namespace MicroChat {
    class ChatSessionTable {
    public:
        ChatSessionTable(std::shared_ptr<odb::core::database> db) : db_(std::move(db)) {}
        bool insert(const std::shared_ptr<ChatSession> &chat_session) {
            try {
                odb::transaction t(db_->begin());
                db_->persist(*chat_session);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Insert chat session failed {}: {}", chat_session->getChatSessionId(), e.what());
                return false;
            }
        }
        std::shared_ptr<ChatSession> get(const std::string &session_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<ChatSession> query;
                typedef odb::result<ChatSession> result;
                result r(db_->query<ChatSession>(query::chat_session_id == session_id));
                if (!r.empty()) {
                    auto chat_session = std::make_shared<ChatSession>(*r.begin());
                    t.commit();
                    return chat_session;
                }
            } catch(const std::exception& e) {
                LOG_ERROR("Fetch chat session failed {}: {}", session_id, e.what());
            }
            return nullptr;
        }
        //会话删除后，相关的成员关系也要删除
        bool del(const std::string &session_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<ChatSession> query;
                typedef odb::result<ChatSession> result;
                result r(db_->query<ChatSession>(query::chat_session_id == session_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    db_->erase(*it);
                }
                typedef odb::query<ChatSessionMember> member_query;
                db_->erase_query<ChatSessionMember>(member_query::session_id == session_id);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Delete chat session failed {}: {}", session_id, e.what());
                return false;
            }
        }
        //删除单聊会话
        bool delSingleChatSession(const std::string &user_id, const std::string &friend_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<SingleChatSession> query;
                auto res = db_->query_one<SingleChatSession>(
                        query::csm1::user_id == user_id && 
                        query::csm2::user_id == friend_id && 
                    query::css::type == ChatSessionType::SINGLE);
                if (!res) {
                    t.commit();
                    return true;
                }
                std::string session_id = res->chat_session_id;
                //删除会话
                typedef odb::query<ChatSession> chat_session_query;
                db_->erase_query<ChatSession>(chat_session_query::chat_session_id == session_id);
                //删除会话成员关系
                typedef odb::query<ChatSessionMember> member_query;
                db_->erase_query<ChatSessionMember>(member_query::session_id == session_id);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Delete single chat session failed {}-{}: {}", user_id, friend_id, e.what());
                return false;
            }
        }
        //获取某用户的所有单聊会话
        std::vector<SingleChatSession> getSingleChatSessions(const std::string &user_id) {
            std::vector<SingleChatSession> sessions;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<SingleChatSession> query;
                typedef odb::result<SingleChatSession> result;
                result r(db_->query<SingleChatSession>(
                        query::csm1::user_id == user_id && 
                        query::csm2::user_id != user_id &&
                        query::css::type == ChatSessionType::SINGLE));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    sessions.push_back(*it);
                }
                t.commit();
            } catch(const std::exception& e) {
                LOG_ERROR("Fetch single chat sessions failed {}: {}", user_id, e.what());
            }
            return sessions;
        }
        //获取某用户的所有群聊会话
        std::vector<GroupChatSession> getGroupChatSessions(const std::string &user_id) {
            std::vector<GroupChatSession> sessions;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<GroupChatSession> query;
                typedef odb::result<GroupChatSession> result;
                result r(db_->query<GroupChatSession>(
                        query::csm::user_id == user_id &&
                        query::css::type == ChatSessionType::GROUP));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    sessions.push_back(*it);
                }
                t.commit();
            } catch(const std::exception& e) {
                LOG_ERROR("Fetch group chat sessions failed {}: {}", user_id, e.what());
            }
            return sessions;
        }
    private:
        std::shared_ptr<odb::core::database> db_;
    };
}