#pragma once
#include "elasearch.hpp"
#include "../odb/user.hxx"
#include "spdlog.hpp"
namespace MicroChat{
    class ESClientBuilder{
    public:
        static std::shared_ptr<elasticlient::Client> createClient(const std::vector<std::string> &hosts){
            return std::make_shared<elasticlient::Client>(hosts);
        };
    };
    //ES管理User相关内容
    class ESUserManager{
    public:
        ESUserManager(const std::shared_ptr<elasticlient::Client> &client):client_(client){}
        //创建用户索引
        bool create_user_index(){
            bool ret = ESIndex(client_, "user")
                .appendproperty("user_id", "keyword", "", true)
                .appendproperty("phone_number", "keyword", "", true)
                .appendproperty("nickname")
                .appendproperty("personal_signature" , "text", "", false)
                .appendproperty("avatar_id" , "keyword", "", false)
                .create();
            return ret;
        }
        //添加和更新用户信息
        bool appenduser(const std::string &user_id,
                                const std::string &phone_number,
                                const std::string &nickname,
                                const std::string &personal_signature,
                                const std::string &avatar_id){
            bool ret = ESInsert(client_, "user")
                .append("user_id", user_id)
                .append("phone_number", phone_number)
                .append("nickname", nickname)
                .append("personal_signature", personal_signature)
                .append("avatar_id", avatar_id)
                .insert(user_id);
            if(!ret){
                LOG_ERROR("向ES索引 user 添加用户信息失败！");
                return false;
            }
            LOG_DEBUG("向ES索引 user 添加用户信息成功！");
            return true;
        }
        //删除用户信息
        bool deleteuser(const std::string &user_id){
            bool ret = ESRemove(client_, "user").remove(user_id);
            if(!ret){
                LOG_ERROR("从ES索引 user 删除用户信息失败！");
                return false;
            }
            LOG_DEBUG("从ES索引 user 删除用户信息成功！");
            return true;
        }
        //搜索用户信息
        std::vector<User> search(const std::string &key, const std::vector<std::string> &uid_list){
            std::vector<User> users;
            Json::Value result = ESSearch(client_, "user")
                    .append_should_match("phone_number", key)
                    .append_should_match("user_id", key)
                    .append_should_match("nickname", key)
                    .append_must_not_terms("user_id", uid_list)
                    .search();
            if(result.isArray() == false){
                LOG_ERROR("ES搜索用户结果格式错误！");
                return users;
            }
            if(result.size() == 0){
                LOG_DEBUG("ES搜索用户无结果！");
                return users;
            }
            int sz = result.size();
            LOG_DEBUG("ES搜索用户命中 {} 条结果！", sz);
            for(int i = 0; i < sz; i++){
                Json::Value source = result[i]["_source"];
                User user;
                user.setUserId(source["user_id"].asString());
                user.setNickname(source["nickname"].asString());
                user.setPhone(source["phone_number"].asString());
                user.setAvatarId(source["avatar_id"].asString());
                user.setPersonalSignature(source["personal_signature"].asString());
                users.push_back(user);
            }
            return users;
        }
    private:
        std::shared_ptr<elasticlient::Client> client_;
    };
}//namespace MicroChat