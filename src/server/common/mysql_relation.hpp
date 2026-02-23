#pragma once
#include <string>
#include "mysql.hpp"
#include "relation.hxx"
#include "relation-odb.hxx"

namespace MicroChat {
    class RelationTable {
    public:
        RelationTable(std::shared_ptr<odb::core::database> db) : db_(std::move(db)) {}
        //好友关系是双向的，所以插入两条记录
        bool insert(const std::shared_ptr<Relation> &relation) {
            try {
                odb::transaction t(db_->begin());
                db_ -> persist(*relation);
                Relation reverse_relation(relation->getFriendId(), relation->getUserId());
                db_ -> persist(reverse_relation);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Insert relation failed {}: {}", relation->getUserId(), e.what());
                return false;
            }
        }
        bool del_relation(const std::string &user_id , const std::string &friend_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<Relation> query;
                typedef odb::result<Relation> result;
                result r(db_->query<Relation>(query::user_id == user_id && query::friend_id == friend_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    db_->erase(*it);
                }
                result r_reverse(db_->query<Relation>(query::user_id == friend_id && query::friend_id == user_id));
                for (auto it = r_reverse.begin(); it != r_reverse.end(); ++it) {
                    db_->erase(*it);
                }
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Delete relations failed {}—{}: {}", user_id, friend_id, e.what());
                return false;
            }
        }
        std::vector<std::string> getFriends(const std::string &user_id) {
            std::vector<std::string> friends;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<Relation> query;
                typedef odb::result<Relation> result;
                result r(db_->query<Relation>(query::user_id == user_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    friends.push_back(it->getFriendId());
                }
                t.commit();
            } catch(const std::exception& e) {
                LOG_ERROR("Fetch friends failed {}: {}", user_id, e.what());
            }
            return friends;
        }
        //判断是否是好友
        bool isFriend(const std::string &user_id, const std::string &friend_id) {
            bool flag = false;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<Relation> query;
                typedef odb::result<Relation> result;
                result r(db_->query<Relation>(query::user_id == user_id && query::friend_id == friend_id));
                if (!r.empty()) {
                    flag = true;
                }
                t.commit();
                return flag;
            } catch(const std::exception& e) {
                LOG_ERROR("Check friendship failed {}—{}: {}", user_id, friend_id, e.what());
                return false;
            }
        }
    private:
        std::shared_ptr<odb::core::database> db_;
    };
}//namespace MicroChat