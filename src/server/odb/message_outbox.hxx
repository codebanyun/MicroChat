#pragma once
#include <string>
#include <odb/core.hxx>
#include <odb/nullable.hxx>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace MicroChat {
    enum class OutboxStatus : unsigned char {
        PENDING = 0,
        PROCESSING = 1,
        SUCCESS = 2,
        FAILED = 3,
        DEAD = 4
    };

    #pragma db object table("message_outbox")
    class MessageOutbox {
    public:
        MessageOutbox() = default;
        MessageOutbox(const std::string &event_id,
                      const std::string &message_id,
                      const std::string &channel,
                      const std::string &payload,
                      unsigned char status,
                      unsigned int retry_count,
                      const boost::posix_time::ptime &next_retry_at)
            : event_id_(event_id),
              message_id_(message_id),
              channel_(channel),
              payload_(payload),
              status_(status),
              retry_count_(retry_count),
              next_retry_at_(next_retry_at) {}

        const std::string &eventId() const { return event_id_; }
        const std::string &messageId() const { return message_id_; }
        const std::string &channel() const { return channel_; }
        const std::string &payload() const { return payload_; }
        unsigned char status() const { return status_; }
        unsigned int retryCount() const { return retry_count_; }
        const boost::posix_time::ptime &nextRetryAt() const { return next_retry_at_; }

        void setStatus(unsigned char status) { status_ = status; }
        void setRetryCount(unsigned int retry_count) { retry_count_ = retry_count; }
        void setNextRetryAt(const boost::posix_time::ptime &next_retry_at) { next_retry_at_ = next_retry_at; }
        void setLastError(const std::string &last_error) { last_error_ = last_error; }

    private:
        friend class odb::access;
        #pragma db id auto
        unsigned long id_;
        #pragma db type("varchar(64)") index unique
        std::string event_id_;
        #pragma db type("varchar(64)") index
        std::string message_id_;
        #pragma db type("varchar(32)")
        std::string channel_;
        #pragma db type("text")
        std::string payload_;
        unsigned char status_;
        unsigned int retry_count_;
        #pragma db type("timestamp")
        boost::posix_time::ptime next_retry_at_;
        odb::nullable<std::string> last_error_;
    };
}
