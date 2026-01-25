#pragma once
#include <string>
#include <cstddef> 
#include <odb/nullable.hxx>
#include <odb/core.hxx>

namespace MicroChat {
    #pragma db object table("user")
    class User {
    public:
        //默认构造函数
        User() = default;
        //用户名--新增用户 -- 用户ID, 昵称，密码
        User(const std::string& user_id,
             const std::string& nickname,
             const std::string& password_hash)
            : user_id_(user_id),
              nickname_(nickname),
              password_hash_(password_hash)
            {}
        //手机号新增用户，给默认昵称
        User(const std::string& user_id,
             const std::string &phone_number)
            : user_id_(user_id),
              nickname_("用户" + (user_id.size() > 4 ? user_id.substr(user_id.size() - 4) : user_id)),
              phone_number_(phone_number)
            {}
        void setUserId(const std::string& user_id) {
            user_id_ = user_id;
        }
        const std::string& getUserId() const {
            return user_id_;
        }
        void setNickname(const std::string& nickname) {
            nickname_ = nickname;
        }
        const std::string& getNickname() const {
            return nickname_;
        }
        void setPasswordHash(const std::string& password_hash) {
            password_hash_ = password_hash;
        }
        std::string getPasswordHash() const {
            if (!password_hash_) return std::string();
            return *password_hash_;
        }
        void setPhone(const std::string& phone) {
            phone_number_ = phone;
        }
        std::string getPhone() const {
            if (!phone_number_) return std::string();
            return *phone_number_;
        }
        void setAvatarId(const std::string& avatar_id) {
            avatar_id_ = avatar_id;
        }
        std::string getAvatarId() const {
            if (!avatar_id_) return std::string();
            return *avatar_id_;
        }
        void setPersonalSignature(const std::string& personal_signature) {
            personal_signature_ = personal_signature;
        }
        std::string getPersonalSignature() const {
            if (!personal_signature_) return std::string();
            return *personal_signature_;
        }
    private:
        friend class odb::access;
        #pragma db id auto
        unsigned long id_;
        #pragma db type("varchar(64)") index unique
        std::string user_id_;
        #pragma db type("varchar(128)") index unique
        std::string nickname_;
        #pragma db type("varchar(255)")
        odb::nullable<std::string> personal_signature_;
        #pragma db type("varchar(128)")
        odb::nullable<std::string> password_hash_; //用户密码 - 不一定存在
        #pragma db type("varchar(64)") index unique
        odb::nullable<std::string> phone_number_ ; //用户手机号 - 不一定存在
        #pragma db type("varchar(64)")
        odb::nullable<std::string> avatar_id_; //用户头像文件ID - 不一定存在        
    };
}

//odb -d mysql --std c++11 --generate-query --generate-schema --profile boost/date-time person.hxx