#include "etcd.hpp"
#include "channel.hpp"
#include <gflags/gflags.h>
#include "aip-cpp-sdk/speech.h"
#include "speech.pb.h"
#include "spdlog.hpp"

DEFINE_bool(run_mode, false, "程序的运行模式，false-调试； true-发布；");
DEFINE_string(log_file, "", "发布模式下，用于指定日志的输出文件");
DEFINE_int32(log_level, 0, "发布模式下，用于指定日志输出等级");

DEFINE_string(etcd_host, "http://127.0.0.1:2379", "服务注册中心地址");
DEFINE_string(base_service, "/service", "服务监控根目录");
DEFINE_string(speech_service, "/service/speech_service", "语音识别服务名称");

int main(int argc, char *argv[])
{
    google::ParseCommandLineFlags(&argc, &argv, true);
    MicroChat::init_logger(FLAGS_run_mode, FLAGS_log_file, FLAGS_log_level);
    //1. 先构造Rpc信道管理对象
    auto channel_manager = std::make_shared<MicroChat::ServiceManager>();
    channel_manager->add_service_name(FLAGS_speech_service);
    auto put_cb = std::bind(&MicroChat::ServiceManager::on_service_online, channel_manager.get(), std::placeholders::_1, std::placeholders::_2);
    auto del_cb = std::bind(&MicroChat::ServiceManager::on_service_offline, channel_manager.get(), std::placeholders::_1, std::placeholders::_2);
    //2. 构造Etcd客户端发现对象
    auto etcd_registry = std::make_shared<MicroChat::EtcdClientfinder>(FLAGS_etcd_host , FLAGS_base_service,
                                                                       put_cb,
                                                                       del_cb);
    //3. 构造Rpc信道对象
    auto channel = channel_manager->get_service_node(FLAGS_speech_service);
    if (!channel) {
        LOG_ERROR("获取 {} 服务信道失败！", FLAGS_speech_service);
        return -1;
    }
    //读取语音文件数据
    std::string file_content;
    aip::get_file_content("16k.pcm", &file_content);
    //4. 构造Rpc存根对象
    MicroChat::SpeechService_Stub stub(channel.get());
    //5. 构造请求对象和响应对象
    MicroChat::SpeechRecognitionReq request;
    MicroChat::SpeechRecognitionRsp response;
    request.set_request_id("test_001");
    request.set_speech_content(file_content);
    //6. 发起Rpc调用
    brpc::Controller cntl;
    stub.SpeechRecognition(&cntl, &request, &response, nullptr);
    if (cntl.Failed()) {
        LOG_ERROR("RPC调用失败: {}", cntl.ErrorText());
        return -1;
    }
    if (!response.success()) {
        LOG_ERROR("语音识别失败: {}", response.errmsg());
        return -1;
    }
    LOG_INFO("语音识别结果: {}", response.recognition_result());
    return 0;
}