#pragma once
#include <string>
#include <cstddef> 
#include <odb/core.hxx>

namespace MicroChat {
    #pragma db object table("chat_session_member")
    class ChatSessionMember {
    public:
        //默认构造函数
        ChatSessionMember() = default;
        //构造函数
        ChatSessionMember(const std::string& session_id,
                          const std::string& user_id)
            : session_id_(session_id),
              user_id_(user_id)
            {}
        void setSessionId(const std::string& session_id) {
            session_id_ = session_id;
        }
        const std::string& getSessionId() const {
            return session_id_;
        }
        void setUserId(const std::string& user_id) {
            user_id_ = user_id;
        }
        const std::string& getUserId() const {
            return user_id_;
        }
    private:
        friend class odb::access;
        #pragma db id auto 
        unsigned long id_;        //自增ID
        #pragma db type("VARCHAR(64)") index
        std::string session_id_; //聊天会话ID
        #pragma db type("VARCHAR(64)") 
        std::string user_id_;    //用户ID
    };
} // namespace MicroChat