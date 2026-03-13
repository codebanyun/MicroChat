#pragma once
#include <string>
#include <algorithm>
#include "mysql.hpp"
#include <odb/boost/date-time/mysql/posix-time-traits.hxx>
#include "message.hxx"
#include "message-odb.hxx"
#include "message_outbox.hxx"
#include "message_outbox-odb.hxx"

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
        bool insertWithOutbox(const std::shared_ptr<Message> &message,
                              const std::shared_ptr<MessageOutbox> &event) {
            try {
                odb::transaction t(db_->begin());
                db_->persist(*message);
                db_->persist(*event);
                t.commit();
                return true;
            } catch (const std::exception &e) {
                LOG_ERROR("Insert message+outbox failed {}: {}", message->getMessageId(), e.what());
                return false;
            }
        }
        bool updateFileInfo(const std::string &message_id,
                            const std::string &file_id,
                            const std::string &file_name,
                            unsigned int file_size) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<Message> query;
                typedef odb::result<Message> result;
                result r(db_->query<Message>(query::message_id == message_id));
                auto it = r.begin();
                if (it == r.end()) {
                    LOG_ERROR("Update file info failed, message not found: {}", message_id);
                    return false;
                }
                Message message = *it;
                message.setFileId(file_id);
                if (!file_name.empty()) {
                    message.setFileName(file_name);
                }
                if (file_size > 0) {
                    message.setFileSize(file_size);
                }
                db_->update(message);
                t.commit();
                return true;
            } catch (const std::exception &e) {
                LOG_ERROR("Update file info failed {}: {}", message_id, e.what());
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
        std::vector<Message> searchByKeyword(const std::string &session_id,
                                             const std::string &keyword,
                                             size_t limit = 100) {
            std::vector<Message> matched_messages;
            if (keyword.empty()) {
                return matched_messages;
            }
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<Message> query;
                typedef odb::result<Message> result;
                size_t scan_limit = std::max<size_t>(limit * 10, 500);
                result r(db_->query<Message>((query::session_id == session_id &&
                                             query::message_type == 0) +
                                             " ORDER BY " + query::timestamp +
                                             " DESC LIMIT " + std::to_string(scan_limit)));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    const auto content = it->getContent();
                    if (content.find(keyword) != std::string::npos) {
                        matched_messages.push_back(*it);
                        if (matched_messages.size() >= limit) {
                            break;
                        }
                    }
                }
                reverse(matched_messages.begin(), matched_messages.end());
                t.commit();
            } catch (const std::exception &e) {
                LOG_ERROR("Fallback keyword search failed {}: {}", session_id, e.what());
            }
            return matched_messages;
        }
    private:
        std::shared_ptr<odb::core::database> db_;
    };
} // namespace MicroChat