#pragma once
#include <string>
#include <cstddef> 
#include <odb/core.hxx>

namespace MicroChat {
    #pragma db object table("relation")
    class Relation {
    public:        
        Relation() = default;
        Relation(const std::string &user_id, const std::string &friend_id)
            : user_id_(user_id), friend_id_(friend_id) {}
        std::string getUserId() const { return user_id_; }
        std::string getFriendId() const { return friend_id_; }
        void setUserId(const std::string &user_id) { user_id_ = user_id; }
        void setFriendId(const std::string &friend_id) { friend_id_ = friend_id; }
    private:
        friend class odb::access;
        #pragma db id auto
        unsigned long id_;
        #pragma db type("varchar(255)") index
        std::string user_id_;
        #pragma db type("varchar(255)") index
        std::string friend_id_;
    };
}//namespace MicroChat