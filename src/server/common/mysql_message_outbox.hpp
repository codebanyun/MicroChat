#pragma once
#include <vector>
#include <string>
#include <memory>
#include "mysql.hpp"
#include <odb/boost/date-time/mysql/posix-time-traits.hxx>
#include "message_outbox.hxx"
#include "message_outbox-odb.hxx"

namespace MicroChat {
    class MessageOutboxTable {
    public:
        MessageOutboxTable(std::shared_ptr<odb::core::database> db) : db_(std::move(db)) {}

        bool insert(const std::shared_ptr<MessageOutbox> &event) {
            try {
                odb::transaction t(db_->begin());
                db_->persist(*event);
                t.commit();
                return true;
            } catch (const std::exception &e) {
                LOG_ERROR("Insert outbox event failed {}: {}", event->eventId(), e.what());
                return false;
            }
        }
        bool updateStatus(const std::string &event_id,
                          unsigned char status,
                          unsigned int retry_count,
                          const boost::posix_time::ptime &next_retry_at,
                          const std::string &last_error = "") {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<MessageOutbox> query;
                typedef odb::result<MessageOutbox> result;
                result r(db_->query<MessageOutbox>(query::event_id == event_id));
                auto it = r.begin();
                if (it == r.end()) {
                    LOG_ERROR("Update outbox status failed, event not found: {}", event_id);
                    return false;
                }
                MessageOutbox event = *it;
                event.setStatus(status);
                event.setRetryCount(retry_count);
                event.setNextRetryAt(next_retry_at);
                if (!last_error.empty()) {
                    event.setLastError(last_error);
                }
                db_->update(event);
                t.commit();
                return true;
            } catch (const std::exception &e) {
                LOG_ERROR("Update outbox status failed {}: {}", event_id, e.what());
                return false;
            }
        }

        std::vector<MessageOutbox> fetchReadyEvents(unsigned char status,
                                                    const boost::posix_time::ptime &deadline,
                                                    size_t limit = 100) {
            std::vector<MessageOutbox> events;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<MessageOutbox> query;
                typedef odb::result<MessageOutbox> result;
                result r(db_->query<MessageOutbox>((query::status == status &&
                                                    query::next_retry_at <= deadline) +
                                                    " ORDER BY " + query::next_retry_at +
                                                    " ASC LIMIT " + std::to_string(limit)));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    events.push_back(*it);
                }
                t.commit();
            } catch (const std::exception &e) {
                LOG_ERROR("Fetch ready outbox events failed: {}", e.what());
            }
            return events;
        }

    private:
        std::shared_ptr<odb::core::database> db_;
    };
}
