#include <gflags/gflags.h>
#include "message_server.hpp"

DEFINE_bool(run_mode, false, "程序的运行模式，false-调试； true-发布；");
DEFINE_string(log_file, "", "发布模式下，用于指定日志的输出文件");
DEFINE_int32(log_level, 0, "发布模式下，用于指定日志输出等级");

DEFINE_string(registry_host, "http://127.0.0.1:2379", "服务注册中心地址");
DEFINE_string(instance_name, "/message_service/instance", "当前实例名称");
DEFINE_string(access_host, "127.0.0.1:10005", "当前实例的外部访问地址");

DEFINE_int32(listen_port, 10005, "Rpc服务器监听端口");
DEFINE_int32(rpc_timeout, -1, "Rpc调用超时时间");
DEFINE_int32(rpc_threads, 1, "Rpc的IO线程数量");


DEFINE_string(base_service, "/service", "服务监控根目录");
DEFINE_string(file_service, "/service/file_service", "文件管理子服务名称");
DEFINE_string(user_service, "/service/user_service", "用户管理子服务名称");

DEFINE_string(es_host, "http://127.0.0.1:9200/", "ES搜索引擎服务器URL");

DEFINE_string(mysql_host, "127.0.0.1", "Mysql服务器访问地址");
DEFINE_string(mysql_user, "root", "Mysql服务器访问用户名");
DEFINE_string(mysql_pswd, "mysql123", "Mysql服务器访问密码");
DEFINE_string(mysql_db, "MicroChat", "Mysql默认库名称");
DEFINE_string(mysql_cset, "utf8", "Mysql客户端字符集");
DEFINE_int32(mysql_port, 0, "Mysql服务器访问端口");
DEFINE_int32(mysql_pool_count, 4, "Mysql连接池最大连接数量");

DEFINE_string(mq_user, "root", "消息队列服务器访问用户名");
DEFINE_string(mq_pswd, "rmq123", "消息队列服务器访问密码");
DEFINE_string(mq_host, "127.0.0.1:5672", "消息队列服务器访问地址");
DEFINE_string(mq_msg_exchange, "msg_exchange", "持久化消息的发布交换机名称");
DEFINE_string(mq_msg_queue, "msg_queue", "持久化消息的发布队列名称");
DEFINE_string(mq_msg_binding_key, "msg_queue", "持久化消息的发布队列名称");
DEFINE_string(mq_retry_exchange, "msg_retry_exchange", "消息重试交换机名称");
DEFINE_string(mq_retry_queue, "msg_retry_queue", "消息重试队列名称");
DEFINE_string(mq_retry_binding_key, "msg_retry_queue", "消息重试绑定键");
DEFINE_string(mq_dead_exchange, "msg_dead_exchange", "消息死信交换机名称");
DEFINE_string(mq_dead_queue, "msg_dead_queue", "消息死信队列名称");
DEFINE_string(mq_dead_binding_key, "msg_dead_queue", "消息死信绑定键");
DEFINE_int32(mq_retry_delay_ms, 10000, "消息重试延迟毫秒");
DEFINE_int32(mq_max_retry_count, 3, "消息最大重试次数");

int main(int argc, char *argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    MicroChat::init_logger(FLAGS_run_mode , FLAGS_log_file , FLAGS_log_level);
    MicroChat::MessageServerFactory factory;
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
    if(!factory.create_rabbitmq_client(
        FLAGS_mq_user,
        FLAGS_mq_pswd,
        FLAGS_mq_host,
        FLAGS_mq_msg_exchange,
        FLAGS_mq_msg_queue,
        FLAGS_mq_msg_binding_key,
        FLAGS_mq_retry_exchange,
        FLAGS_mq_retry_queue,
        FLAGS_mq_retry_binding_key,
        FLAGS_mq_dead_exchange,
        FLAGS_mq_dead_queue,
        FLAGS_mq_dead_binding_key,
        FLAGS_mq_retry_delay_ms,
        FLAGS_mq_max_retry_count)){
        LOG_ERROR("Failed to create RabbitMQ client");
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
        FLAGS_file_service)){
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
    auto message_server = factory.create_message_server();
    if(!message_server) {
        LOG_ERROR("Failed to create Message server");
        return -1;
    }
    message_server->start();
    MicroChat::shutdown_logger();
    return 0;
}