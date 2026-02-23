#pragma once
#include <string>
#include <cstddef> 
#include <odb/core.hxx>
#include <odb/nullable.hxx>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace MicroChat {
    #pragma db object table("friend_request")
    class FriendRequest {
    public:
        FriendRequest() = default;
        FriendRequest(const std::string& event_id, const std::string &requester_id, const std::string &receiver_id, const boost::posix_time::ptime &timestamp = boost::posix_time::second_clock::universal_time() , const std::string &message = "申请添加您为好友")
            : event_id_(event_id), requester_id_(requester_id), receiver_id_(receiver_id), message_(message), timestamp_(timestamp) {}
        std::string getRequesterId() const { return requester_id_; }
        std::string getReceiverId() const { return receiver_id_; }
        std::string getMessage() const { return message_; }
        std::string getEventId() const { return event_id_; }
        boost::posix_time::ptime getTimestamp() const { return timestamp_; }
        void setRequesterId(const std::string &requester_id) { requester_id_ = requester_id; }
        void setReceiverId(const std::string &receiver_id) { receiver_id_ = receiver_id; }
        void setMessage(const std::string &message) { message_ = message; }
        void setEventId(const std::string &event_id) { event_id_ = event_id; }
        void setTimestamp(const boost::posix_time::ptime &timestamp) { timestamp_ = timestamp; }
    private:
        friend class odb::access;
        #pragma db id auto
        unsigned long id_;
        #pragma db type("varchar(255)") index
        std::string requester_id_;
        #pragma db type("varchar(255)") index
        std::string receiver_id_;
        #pragma db type("varchar(255)") index unique
        std::string event_id_;
        std::string message_;
        #pragma db type("timestamp")
        boost::posix_time::ptime timestamp_;
    };
}//namespace MicroChat