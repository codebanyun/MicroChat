#pragma once
#include <brpc/server.h>
#include <butil/logging.h>
#include "etcd.hpp"     // 服务注册模块封装
#include "spdlog.hpp"   // 日志模块封装
#include "utils.hpp"    // 工具模块封装
#include "file.pb.h"  // protobuf框架代码
#include "base.pb.h"  // protobuf框架代码

namespace MicroChat { 
    class FileServiceImpl : public FileService {
        public:
            FileServiceImpl(const std::string &storage_path) : storage_path_(storage_path) {
                if (storage_path_.empty()) storage_path_ = "./";
                if (storage_path_.back() != '/') storage_path_ += "/";
                // 创建存储目录（如果不存在）
                umask(0);
                std::string cmd = "mkdir -p " + storage_path_;
                if (system(cmd.c_str()) != 0) {
                    LOG_ERROR("Failed to create storage directory: {}", storage_path_);
                }
            }
            ~FileServiceImpl() {}
            void PutSingleFile(google::protobuf::RpcController* controller,
                                const MicroChat::PutSingleFileReq* request,
                                MicroChat::PutSingleFileRsp* response,
                                google::protobuf::Closure* done) override
            {
                brpc::ClosureGuard done_guard(done);
                // 文件上传逻辑
                LOG_INFO("Received file upload request: file_name={}, file_size={}", 
                          request->file_data().file_name(), request->file_data().file_size());
                //生成唯一文件ID
                std::string file_id = UUID();
                //保存文件到本地存储目录
                std::string file_path = storage_path_ + file_id ;
                //写文件
                bool write_success = writeFile(file_path, request->file_data().file_content());
                if(!write_success){
                    response->set_success(false);
                    LOG_ERROR("Failed to save uploaded file: {}", file_path);
                    return;
                }
                // 文件上传成功,组织响应
                response->set_request_id(request->request_id());
                response->set_success(true);
                response->mutable_file_info()->set_file_id(file_id);
                response->mutable_file_info()->set_file_name(request->file_data().file_name());
                response->mutable_file_info()->set_file_size(request->file_data().file_size());
                LOG_INFO("File upload completed: file_id={}", response->file_info().file_id());
            }
            void PutMultiFile(google::protobuf::RpcController* controller,
                                const MicroChat::PutMultiFileReq* request,
                                MicroChat::PutMultiFileRsp* response,
                                google::protobuf::Closure* done) override
            {
                brpc::ClosureGuard done_guard(done);
                response->set_request_id(request->request_id());
                // 文件批量上传逻辑
                LOG_INFO("Received multi-file upload request: file_count={}", request->file_data_size());
                for (const auto& file_data : request->file_data()) {
                    LOG_INFO("Processing file: file_name={}, file_size={}", 
                              file_data.file_name(), file_data.file_size());
                    //生成唯一文件ID
                    std::string file_id = UUID();
                    //保存文件到本地存储目录
                    std::string file_path = storage_path_ + file_id ;
                    //写文件
                    bool write_success = writeFile(file_path, file_data.file_content());
                    if(!write_success){
                        LOG_ERROR("Failed to save uploaded file: {}", file_path);
                        response->set_success(false);
                        response->set_errmsg("Failed to save file");
                        return;
                    }
                    // 文件上传成功,组织响应
                    auto* file_info = response->add_file_info();
                    file_info->set_file_id(file_id);
                    file_info->set_file_name(file_data.file_name());
                    file_info->set_file_size(file_data.file_size());
                    LOG_INFO("File upload completed: file_id={}", file_info->file_id());
                }
                response->set_success(true);
            }
            void GetSingleFile(google::protobuf::RpcController* controller,
                                const MicroChat::GetSingleFileReq* request,
                                MicroChat::GetSingleFileRsp* response,
                                google::protobuf::Closure* done) override
            {
                brpc::ClosureGuard done_guard(done);
                // 文件下载逻辑
                LOG_INFO("Received file download request: file_id={}", request->file_id());
                // 构造文件路径
                std::string file_path = storage_path_ + request->file_id();
                // 读取文件内容
                std::string file_content;
                bool read_success = readFile(file_path, file_content);
                if(!read_success){
                    response->set_success(false);
                    response->set_errmsg("Failed to read requested file");
                    LOG_ERROR("Failed to read requested file: {}", file_path);
                    return;
                }
                // 文件读取成功,组织响应
                response->set_request_id(request->request_id());
                response->set_success(true);
                response->mutable_file_data()->set_file_id(request->file_id());
                response->mutable_file_data()->set_file_content(file_content);
                LOG_INFO("File download completed: file_id={}", response->file_data().file_id());
            }
            void GetMultiFile(google::protobuf::RpcController* controller,
                                const MicroChat::GetMultiFileReq* request,
                                MicroChat::GetMultiFileRsp* response,
                                google::protobuf::Closure* done) override
            {
                brpc::ClosureGuard done_guard(done);
                response->set_request_id(request->request_id());
                // 文件批量下载逻辑
                LOG_INFO("Received multi-file download request: file_count={}", request->file_id_list_size());
                for (const auto& file_id : request->file_id_list()) {
                    LOG_INFO("Processing file download: file_id={}", file_id);
                    // 构造文件路径
                    std::string file_path = storage_path_ + file_id;
                    // 读取文件内容
                    std::string file_content;
                    bool read_success = readFile(file_path, file_content);
                    if(!read_success){
                        LOG_ERROR("Failed to read requested file: {}", file_path);
                        response->set_success(false);
                        response->set_errmsg("Failed to read requested file");
                        return;
                    }
                    // 文件读取成功,组织响应
                    FileDownloadData data;
                    data.set_file_id(file_id);
                    data.set_file_content(file_content);
                    response->mutable_file_data()->insert({file_id, std::move(data)});
                    LOG_INFO("File download completed: file_id={}", data.file_id());
                }
                response->set_success(true);
            }
        private:
            std::string storage_path_;
    };
    class FileServer{
        public:
            FileServer(const std::shared_ptr<MicroChat::EtcdClientRegistry>& etcd_registry,
                         const std::shared_ptr<brpc::Server>& brpc_server)
                : etcd_registry_(etcd_registry),
                  brpc_server_(brpc_server) 
                {}
            ~FileServer() {}
            //启动服务
            void Start() {
                brpc_server_ -> RunUntilAskedToQuit();
            }
        private:
            std::shared_ptr<MicroChat::EtcdClientRegistry> etcd_registry_;
            std::shared_ptr<brpc::Server> brpc_server_;
    };
    class FileServerBuilder{
        public:
            FileServerBuilder() {}
            ~FileServerBuilder() {}
            //构建文件服务器
            std::shared_ptr<FileServer> Build(const std::string &etcd_addr,
                                              const std::string &service_name,
                                              const std::string &service_addr,
                                              const std::string &storage_path = "./data/file_storage",
                                              uint16_t port = 8080,
                                              uint8_t thread_num = 1,
                                              int timeout = 5) 
            {
                //创建etcd服务注册模块
                auto etcd_registry = std::make_shared<MicroChat::EtcdClientRegistry>(etcd_addr);
                etcd_registry->register_service(service_name, service_addr, 5);
                //创建brpc服务器
                auto brpc_server = std::make_shared<brpc::Server>();
                //注册文件服务实现
                FileServiceImpl* file_service_impl = new FileServiceImpl(storage_path);
                if (brpc_server->AddService(file_service_impl, brpc::ServiceOwnership::SERVER_OWNS_SERVICE) != 0) {
                    LOG_ERROR("Failed to add FileService to brpc server");
                    return nullptr;
                }
                brpc::ServerOptions options;
                options.num_threads = thread_num;
                options.idle_timeout_sec = timeout;
                if (brpc_server->Start(port, &options) != 0) {
                    LOG_ERROR("Failed to start brpc server on port {}", port);
                    return nullptr;
                }
                //返回文件服务器实例
                return std::make_shared<FileServer>(etcd_registry, brpc_server);
            }
    };
} //namespace MicroChat