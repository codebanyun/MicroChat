#pragma once
#include <string>
#include "mysql.hpp"
#include <odb/boost/date-time/mysql/posix-time-traits.hxx>
#include "message.hxx"
#include "message-odb.hxx"

namespace MicroChat {
    class MessageTable {
    public:
        MessageTable(std::shared_ptr<odb::core::database> db) : db_(std::move(db)) {}
        bool insert(const std::shared_ptr<Message> &message) {
            try {
                odb::transaction t(db_->begin());
                db_ -> persist(*message);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Insert message failed {}: {}", message->getMessageId(), e.what());
                return false;
            }
        }
        bool del_by_session(const std::string &session_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<Message> query;
                typedef odb::result<Message> result;
                result r(db_->query<Message>(query::session_id == session_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    db_->erase(*it);
                }
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Delete messages by session failed {}: {}", session_id, e.what());
                return false;
            }
        }
        std::vector<Message> recentNMessages(const std::string &session_id, size_t n) {
            std::vector<Message> messages;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<Message> query;
                typedef odb::result<Message> result;
                result r(db_->query<Message>((query::session_id == session_id) + " ORDER BY " + query::timestamp + " DESC LIMIT " + std::to_string(n)));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    messages.push_back(*it);
                }
                reverse(messages.begin(), messages.end());
                t.commit();
            } catch(const std::exception& e) {
                LOG_ERROR("Fetch recent messages failed {}: {}", session_id, e.what());
            }
            return messages;
        }
        std::vector<Message> rangeMessages(const std::string &session_id, boost::posix_time::ptime begin_ts, boost::posix_time::ptime end_ts) {
            std::vector<Message> messages;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<Message> query;
                typedef odb::result<Message> result;
                result r(db_->query<Message>((query::session_id == session_id &&
                                             query::timestamp >= begin_ts &&
                                             query::timestamp <= end_ts) + " ORDER BY " + query::timestamp + " ASC"));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    messages.push_back(*it);
                }
                t.commit();
            } catch(const std::exception& e) {
                LOG_ERROR("Fetch range messages failed {}: {}", session_id, e.what());
            }
            return messages;
        }
    private:
        std::shared_ptr<odb::core::database> db_;
    };
} // namespace MicroChat