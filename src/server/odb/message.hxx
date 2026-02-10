#pragma once
#include <string>
#include <cstddef> 
#include <odb/nullable.hxx>
#include <odb/core.hxx>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace MicroChat {
    #pragma db object table("message")
    class Message {
    public:
        Message() = default;
        Message(const std::string &message_id,
                const std::string &session_id,
                const std::string &user_id,
                unsigned char message_type,
                const boost::posix_time::ptime &timestamp,
                const odb::nullable<std::string> &content,
                const odb::nullable<std::string> &file_id,
                const odb::nullable<std::string> &file_name,
                const odb::nullable<unsigned int> &file_size)
            : message_id_(message_id),
              session_id_(session_id),
              user_id_(user_id),
              message_type_(message_type),
              timestamp_(timestamp),
              content_(content),
              file_id_(file_id),
              file_name_(file_name),
              file_size_(file_size) {}
        void setMessageId(const std::string &message_id) {
            message_id_ = message_id;
        }
        void setSessionId(const std::string &session_id) {
            session_id_ = session_id;
        }
        void setUserId(const std::string &user_id) {
            user_id_ = user_id;
        }
        void setMessageType(unsigned char message_type) {
            message_type_ = message_type;
        }
        void setTimestamp(const boost::posix_time::ptime &timestamp) {
            timestamp_ = timestamp;
        }
        void setContent(const std::string& content) {
            content_ = content;
        }
        void setFileId(const std::string &file_id) {
            file_id_ = file_id;
        }
        void setFileName(const std::string &file_name) {
            file_name_ = file_name;
        }
        void setFileSize(unsigned int file_size) {
            file_size_ = file_size;
        }
        const std::string& getMessageId() const { return message_id_; }
        const std::string& getSessionId() const { return session_id_; }
        const std::string& getUserId() const { return user_id_; }
        unsigned char getMessageType() const { return message_type_; }
        const boost::posix_time::ptime& getTimestamp() const { return timestamp_; }
        const std::string getContent() const { 
            if (!content_) return std::string();
            return *content_;
        }
        const std::string getFileId() const { 
            if (!file_id_) return std::string();
            return *file_id_;
        }
        const std::string getFileName() const { 
            if (!file_name_) return std::string();
            return *file_name_;
        }
        unsigned int getFileSize() const { 
            if (!file_size_) return 0;
            return *file_size_;
        }
    private:
        friend class odb::access;
        #pragma db id auto
        unsigned long id_;
        #pragma db type("varchar(64)") index unique
        std::string message_id_;
        #pragma db type("varchar(64)") index
        std::string session_id_;
        #pragma db type("varchar(64)") 
        std::string user_id_;
        unsigned char message_type_; //0:文本 1:图片 2:文件 3:语音
        #pragma db type("timestamp")
        boost::posix_time::ptime timestamp_;
        odb::nullable<std::string> content_; //文本内容
        #pragma db type("varchar(64)")
        odb::nullable<std::string> file_id_; //媒体文件ID
         #pragma db type("varchar(64)")
        odb::nullable<std::string> file_name_; //文件名
        odb::nullable<unsigned int> file_size_; //文件大小    
    };
}// namespace MicroChat