#pragma once
#include <string>
#include <memory> 
#include <cstdlib> // std::exit
#include <iostream>
#include <odb/database.hxx>
#include <odb/mysql/database.hxx>
#include "spdlog.hpp"

namespace MicroChat {
    //创建MySQL数据库连接
    class MySQLDatabaseBuilder {
    public:
        static std::shared_ptr<odb::core::database> createDatabase(
            const std::string& db_name,
            const std::string& user,
            const std::string& password,
            const std::string& host = "127.0.0.1",
            unsigned int port = 3306,
            const std::string &cset = "utf8",
            int conn_pool_count = 1) {
            try {
                std::unique_ptr<odb::mysql::connection_pool_factory> pool_factory(
                    new odb::mysql::connection_pool_factory(conn_pool_count, 0));
                auto res = std::make_shared<odb::mysql::database>(
                    user,
                    password,
                    db_name,
                    host,
                    port,
                    nullptr, // socket
                    cset,
                    0,
                    std::move(pool_factory));
                return res;
            } catch (const odb::exception& e) {
                LOG_ERROR("Failed to create database connection pool: {}", e.what());
                std::exit(EXIT_FAILURE);
            }
        }
    };
}