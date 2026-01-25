#include "mysql.hpp"
#include "user.hxx"
#include "user-odb.hxx"
#include <sstream>

namespace MicroChat {
    class UserTable {
    public:
        UserTable(std::shared_ptr<odb::core::database> db) : db_(std::move(db)) {}
        bool insert(const std::shared_ptr<User> &user) {
            try {
                odb::transaction t(db_->begin());
                db_ -> persist(*user);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Insert user failed {}: {}", user->getNickname(), e.what());
                return false;
            }
        }
        bool update(const std::shared_ptr<User> &user) {
            try {
                odb::transaction t(db_->begin());
                db_ -> update(*user);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Update user failed {}: {}", user->getNickname(), e.what());
                return false;
            }
        }
        std::shared_ptr<User> select_by_nickname(const std::string &nickname) {
            std::shared_ptr<User> res;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<User> query;
                typedef odb::result<User> result;
                res.reset(db_->query_one<User>(query::nickname == nickname));
                t.commit();
                return res;
            } catch(const std::exception& e) {
                LOG_ERROR("Select user failed {}: {}", nickname, e.what());
                return nullptr;
            }
        }
        std::shared_ptr<User> select_by_phone(const std::string &phone) {
            std::shared_ptr<User> res;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<User> query;
                typedef odb::result<User> result;
                res.reset(db_->query_one<User>(query::phone_number == phone));
                t.commit();
                return res;
            } catch(const std::exception& e) {
                LOG_ERROR("Select user failed {}: {}", phone, e.what());
                return nullptr;
            }
        }
        std::shared_ptr<User> select_by_id(const std::string &user_id) {
            std::shared_ptr<User> res;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<User> query;
                res.reset(db_->query_one<User>(query::user_id == user_id));
                t.commit();
                return res;
            } catch(const std::exception& e) {
                LOG_ERROR("Select user failed {}: {}", user_id, e.what());
                return nullptr;
            }
        }
        
        std::vector<User> select_multi_users(const std::vector<std::string> &id_list) {
            if (id_list.empty()) {
                return std::vector<User>();
            }
            std::vector<User> res;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<User> query;
                typedef odb::result<User> result;
                std::stringstream ss;
                ss << "user_id in (";
                for (const auto &id : id_list) {
                    ss << "'" << id << "',";
                }
                std::string condition = ss.str();
                condition.pop_back();
                condition += ")";
                result r(db_->query<User>(condition));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    res.push_back(*it);
                }
                t.commit();
                return res;
            } catch(const std::exception& e) {
                LOG_ERROR("Select multi users failed: {}", e.what());
                return std::vector<User>();
            }
        }
    private:
        std::shared_ptr<odb::core::database> db_;
    };
}