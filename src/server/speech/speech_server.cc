#include "speech_server.hpp"

DEFINE_bool(run_mode, false, "程序的运行模式，false-调试； true-发布；");
DEFINE_string(log_file, "", "发布模式下，用于指定日志的输出文件");
DEFINE_int32(log_level, 0, "发布模式下，用于指定日志输出等级");

DEFINE_string(registry_host, "http://127.0.0.1:2379", "服务注册中心地址");
DEFINE_string(base_service, "/service", "服务监控根目录");
DEFINE_string(instance_name, "/speech_service/instance", "当前实例名称");
DEFINE_string(access_host, "127.0.0.1:10001", "当前实例的外部访问地址");

DEFINE_int32(listen_port, 10001, "Rpc服务器监听端口");
DEFINE_int32(rpc_timeout, -1, "Rpc调用超时时间");
DEFINE_int32(rpc_threads, 1, "Rpc的IO线程数量");

DEFINE_string(app_id, "7306672", "语音平台应用ID");
DEFINE_string(api_key, "0WbqvaXRsqVuzD91TrXUOK0d", "语音平台API密钥");
DEFINE_string(secret_key, "aDg4I06odqh5RUnaYXPLtmDa8jedTQ6P", "语音平台加密密钥");

int main(int argc , char* argv[])
{
    // 解析命令行参数并初始化日志
    google::ParseCommandLineFlags(&argc, &argv, true);
    MicroChat::init_logger(FLAGS_run_mode, FLAGS_log_file, FLAGS_log_level);

    // 组装服务组件
    MicroChat::SpeechServerFactory factory;

    // 1) 创建 ASR 客户端
    factory.make_asr_object(FLAGS_app_id, FLAGS_api_key, FLAGS_secret_key);

    // 2) 注册到 etcd（service_name 用 base_service + instance_name，service_host 用 access_host）
    std::string service_name = FLAGS_base_service + FLAGS_instance_name;
    factory.make_etcd_registry(FLAGS_registry_host, service_name, FLAGS_access_host, 3);

    // 3) 启动 brpc 服务
    factory.make_brpc_server(static_cast<uint16_t>(FLAGS_listen_port),
                             FLAGS_rpc_threads,
                             FLAGS_rpc_timeout);

    // 4) 获取并运行服务器
    auto server = factory.get_speech_server();
    if (!server) {
        LOG_ERROR("SpeechServer 初始化失败");
        return -1;
    }

    LOG_INFO("SpeechServer 已启动，监听端口: {}", FLAGS_listen_port);
    server->Start();  // 阻塞运行，直到收到退出信号
    LOG_INFO("SpeechServer 已退出");
    return 0;
}