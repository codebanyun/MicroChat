#include "file_server.hpp"
#include <gflags/gflags.h>

DEFINE_bool(run_mode, false, "程序的运行模式，false-调试； true-发布；");
DEFINE_string(log_file, "", "发布模式下，用于指定日志的输出文件");
DEFINE_int32(log_level, 0, "发布模式下，用于指定日志输出等级");

DEFINE_string(registry_host, "http://127.0.0.1:2379", "服务注册中心地址");
DEFINE_string(base_service, "/service", "服务监控根目录");
DEFINE_string(instance_name, "/file_service/instance", "当前实例名称");
DEFINE_string(access_host, "127.0.0.1:10002", "当前实例的外部访问地址");

DEFINE_int32(listen_port, 10002, "Rpc服务器监听端口");
DEFINE_int32(rpc_timeout, -1, "Rpc调用超时时间");
DEFINE_int32(rpc_threads, 1, "Rpc的IO线程数量");

DEFINE_string(storage_path, "./data/file_storage", "当前实例的外部访问地址");

int main(int argc, char* argv[]) {
    // 解析命令行参数并初始化日志
    google::ParseCommandLineFlags(&argc, &argv, true);
    MicroChat::init_logger(FLAGS_run_mode, FLAGS_log_file, FLAGS_log_level);
    // 组装服务组件
    MicroChat::FileServerBuilder builder;
    auto file_server = builder.Build(FLAGS_registry_host,
                                      FLAGS_base_service + FLAGS_instance_name,
                                      FLAGS_access_host,
                                      FLAGS_storage_path,
                                      static_cast<uint16_t>(FLAGS_listen_port),
                                      static_cast<uint8_t>(FLAGS_rpc_threads),
                                      FLAGS_rpc_timeout);
    if (!file_server) {
        LOG_ERROR("FileServer 初始化失败");
        return -1;
    }
    file_server->Start();  // 阻塞运行，直到收到退出信号
    LOG_INFO("FileServer 已退出");
    return 0;
}