#pragma once
#include <string>
#include <odb/core.hxx>
#include "chat_session_member.hxx"

namespace MicroChat {

    enum class ChatSessionType {
        SINGLE = 1,
        GROUP = 2
    };

    #pragma db object table("chat_session")
    class ChatSession {
    public:        
        ChatSession() = default;
        ChatSession(const std::string& chat_session_id, const std::string& chat_session_name, ChatSessionType type)
            : chat_session_id_(chat_session_id), chat_session_name_(chat_session_name), type_(type) {}
        ~ChatSession() = default;
        std::string getChatSessionId() const {
            return chat_session_id_;
        }
        std::string getChatSessionName() const {
            return chat_session_name_;
        }
        ChatSessionType getChatSessionType() const {
            return type_;
        }
        void setChatSessionName(const std::string& chat_session_name) {
            chat_session_name_ = chat_session_name;
        }
        void setChatSessionType(ChatSessionType type) {
            type_ = type;
        }
        void setChatSessionId(const std::string& chat_session_id) {
            chat_session_id_ = chat_session_id;
        }
    private:
        friend class odb::access;
        #pragma db id auto
        unsigned long id_;
        #pragma db type("varchar(64)") index unique
        std::string chat_session_id_;
        #pragma db type("varchar(64)")
        std::string chat_session_name_;
        #pragma db type("tinyint")
        ChatSessionType type_;
    };
    // css::chat_session_type==1 && csm1.user_id=uid && csm2.user_id != csm1.user_id
    #pragma db view object(ChatSession = css)\
                    object(ChatSessionMember = csm1 : css::chat_session_id_ == csm1::session_id_)\
                    object(ChatSessionMember = csm2 : css::chat_session_id_ == csm2::session_id_)\
                    query((?))
    struct SingleChatSession {
        #pragma db column(css::chat_session_id_)
        std::string chat_session_id;
        #pragma db column(csm2::user_id_)
        std::string friend_id;
    };

    // css::chat_session_type==2 && csm.user_id=uid
    #pragma db view object(ChatSession = css)\
                    object(ChatSessionMember = csm : css::chat_session_id_ == csm::session_id_)\
                    query((?))
    struct GroupChatSession {
        #pragma db column(css::chat_session_id_)
        std::string chat_session_id;
        #pragma db column(css::chat_session_name_)
        std::string chat_session_name;
    };

}//namespace MicroChat