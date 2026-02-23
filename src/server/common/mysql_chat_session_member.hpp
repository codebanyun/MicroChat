#pragma once
#include <string>
#include "mysql.hpp"
#include "chat_session_member.hxx"
#include "chat_session_member-odb.hxx"

namespace MicroChat {
    class ChatSessionMemberTable {
    public:
        ChatSessionMemberTable(std::shared_ptr<odb::core::database> db) : db_(std::move(db)) {}
        bool insert(const std::shared_ptr<ChatSessionMember> &member) {
            try {
                odb::transaction t(db_->begin());
                db_ -> persist(*member);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Insert chat session member failed {}: {}", member->getUserId(), e.what());
                return false;
            }
        }
        //插入多个成员,函数重载
        bool insert(const std::vector<std::shared_ptr<ChatSessionMember>> &members) {
            try {
                odb::transaction t(db_->begin());
                for (const auto &member : members) {
                    db_ -> persist(*member);
                }
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Insert multiple chat session members failed: {}", e.what());
                return false;
            }
        }
        bool remove_by_session_and_user(const std::string &session_id, const std::string &user_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<ChatSessionMember> query;
                typedef odb::result<ChatSessionMember> result;
                result r(db_->query<ChatSessionMember>(query::session_id == session_id && query::user_id == user_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    db_->erase(*it);
                }
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Remove chat session member failed {}: {}", user_id, e.what());
                return false;
            }
        }
        //删除某个会话的所有成员
        bool remove_by_session(const std::string &session_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<ChatSessionMember> query;
                typedef odb::result<ChatSessionMember> result;
                result r(db_->query<ChatSessionMember>(query::session_id == session_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    db_->erase(*it);;
                }
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Remove chat session members failed {}: {}", session_id, e.what());
                return false;
            }
        }
        //获取某个会话的所有成员
        std::vector<std::string> getMembers(const std::string &session_id) {
            std::vector<std::string> res;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<ChatSessionMember> query;
                typedef odb::result<ChatSessionMember> result;
                result r(db_->query<ChatSessionMember>(query::session_id == session_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    res.push_back(it->getUserId());
                }
                t.commit();
                return res;
            } catch(const std::exception& e) {
                LOG_ERROR("Select chat session members failed {}: {}", session_id, e.what());
                return {};
            }
        }
    private:
        std::shared_ptr<odb::core::database> db_;
    };
} // namespace MicroChat