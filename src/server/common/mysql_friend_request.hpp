#pragma once
#include <string>
#include "mysql.hpp"
#include "friend_request.hxx"
#include "friend_request-odb.hxx"

namespace MicroChat {
    class FriendRequestTable {
    public:
        FriendRequestTable(std::shared_ptr<odb::core::database> db) : db_(std::move(db)) {}
        bool insert(const std::shared_ptr<FriendRequest> &friend_request) {
            try {
                odb::transaction t(db_->begin());
                db_->persist(*friend_request);
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Insert friend request failed {}: {}", friend_request->getRequesterId(), e.what());
                return false;
            }
        }
        std::vector<FriendRequest> getPendingRequests(const std::string &receiver_id) {
            std::vector<FriendRequest> requests;
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<FriendRequest> query;
                typedef odb::result<FriendRequest> result;
                result r(db_->query<FriendRequest>(query::receiver_id == receiver_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    requests.push_back(*it);
                }
                t.commit();
            } catch(const std::exception& e) {
                LOG_ERROR("Fetch friend requests failed {}: {}", receiver_id, e.what());
            }
            return requests;
        }
        bool del(const std::string &event_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<FriendRequest> query;
                typedef odb::result<FriendRequest> result;
                result r(db_->query<FriendRequest>(query::event_id == event_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    db_->erase(*it);
                }
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Delete friend request failed {}: {}", event_id, e.what());
                return false;
            }
        }
        bool del(const std::string &requester_id, const std::string &receiver_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<FriendRequest> query;
                typedef odb::result<FriendRequest> result;
                result r(db_->query<FriendRequest>(query::requester_id == requester_id && query::receiver_id == receiver_id));
                for (auto it = r.begin(); it != r.end(); ++it) {
                    db_->erase(*it);
                }
                t.commit();
                return true;
            } catch(const std::exception& e) {
                LOG_ERROR("Delete friend request failed {}—{}: {}", requester_id, receiver_id, e.what());
                return false;
            }
        }
        //判断是否存在未处理的好友请求
        bool exists_pending_request(const std::string &requester_id, const std::string &receiver_id) {
            try {
                odb::transaction t(db_->begin());
                typedef odb::query<FriendRequest> query;
                typedef odb::result<FriendRequest> result;
                result r(db_->query<FriendRequest>(query::requester_id == requester_id && query::receiver_id == receiver_id));
                t.commit();
                return !r.empty();
            } catch(const std::exception& e) {
                LOG_ERROR("Check pending friend request failed {}—{}: {}", requester_id, receiver_id, e.what());
                return false;
            }
        }
    private:
        std::shared_ptr<odb::core::database> db_;
    };
}