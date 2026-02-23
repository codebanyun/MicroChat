#include <gflags/gflags.h>
#include "friend_server.hpp"

DEFINE_bool(run_mode, false, "程序的运行模式，false-调试； true-发布；");
DEFINE_string(log_file, "", "发布模式下，用于指定日志的输出文件");
DEFINE_int32(log_level, 0, "发布模式下，用于指定日志输出等级");

DEFINE_string(registry_host, "http://127.0.0.1:2379", "服务注册中心地址");
DEFINE_string(instance_name, "/friend_service/instance", "当前实例名称");
DEFINE_string(access_host, "127.0.0.1:10006", "当前实例的外部访问地址");

DEFINE_int32(listen_port, 10006, "Rpc服务器监听端口");
DEFINE_int32(rpc_timeout, -1, "Rpc调用超时时间");
DEFINE_int32(rpc_threads, 1, "Rpc的IO线程数量");


DEFINE_string(base_service, "/service", "服务监控根目录");
DEFINE_string(user_service, "/service/user_service", "用户管理子服务名称");
DEFINE_string(message_service, "/service/message_service", "消息存储子服务名称");

DEFINE_string(es_host, "http://127.0.0.1:9200/", "ES搜索引擎服务器URL");

DEFINE_string(mysql_host, "127.0.0.1", "Mysql服务器访问地址");
DEFINE_string(mysql_user, "root", "Mysql服务器访问用户名");
DEFINE_string(mysql_pswd, "mysql123", "Mysql服务器访问密码");
DEFINE_string(mysql_db, "MicroChat", "Mysql默认库名称");
DEFINE_string(mysql_cset, "utf8", "Mysql客户端字符集");
DEFINE_int32(mysql_port, 0, "Mysql服务器访问端口");
DEFINE_int32(mysql_pool_count, 4, "Mysql连接池最大连接数量");

int main(int argc, char *argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    MicroChat::init_logger(FLAGS_run_mode , FLAGS_log_file , FLAGS_log_level);
    MicroChat::FriendServerFactory factory;
    if(!factory.create_es_client({FLAGS_es_host})){
        LOG_ERROR("Failed to create ES client");
        return -1;
    }
    if(!factory.create_mysql_client(
        FLAGS_mysql_db,
        FLAGS_mysql_user,
        FLAGS_mysql_pswd,
        FLAGS_mysql_host,
        FLAGS_mysql_port == 0 ? 3306 : FLAGS_mysql_port,
        FLAGS_mysql_cset,
        FLAGS_mysql_pool_count)){
        LOG_ERROR("Failed to create Mysql client");
        return -1;
    }
    if(!factory.create_etcd_register_clients(
        FLAGS_registry_host,
        FLAGS_base_service + FLAGS_instance_name,
        FLAGS_access_host)){
        LOG_ERROR("Failed to create Etcd register client");
        return -1;
    }
    if(!factory.create_etcd_finder_clients(
        FLAGS_registry_host,
        FLAGS_base_service,
        FLAGS_user_service,
        FLAGS_message_service)){
        LOG_ERROR("Failed to create Etcd finder client");
        return -1;
    }
    if(!factory.create_brpc_server(
        FLAGS_listen_port,
        FLAGS_rpc_timeout,
        FLAGS_rpc_threads)){
        LOG_ERROR("Failed to create brpc server");
        return -1;
    }
    auto friend_server = factory.create_friend_server();
    if(!friend_server) {
        LOG_ERROR("Failed to create Friend server");
        return -1;
    }
    friend_server->start();
    MicroChat::shutdown_logger();
    return 0;
}