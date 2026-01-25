#pragma once 
#include <brpc/server.h>
#include <butil/logging.h>
#include <cctype>
#include "es_manager.hpp"      // es数据管理客户端封装
#include "redis_manager.hpp"      // redis数据管理客户端封装
#include "mysql_user.hpp"      // mysql数据管理客户端封装
#include "etcd.hpp"     // 服务注册模块封装
#include "spdlog.hpp"   // 日志模块封装
#include "utils.hpp"    // 基础工具接口
#include "channel.hpp"  // 信道管理模块封装
#include "user.hxx"
#include "user-odb.hxx"
#include "user.pb.h"  // protobuf框架代码
#include "base.pb.h"  // protobuf框架代码
#include "file.pb.h"  // protobuf框架代码

namespace MicroChat {
    class UserServiceImpl : public MicroChat::UserService {
    public:
        UserServiceImpl(const std::shared_ptr<elasticlient::Client> &es_client,
                    const std::shared_ptr<odb::core::database> &mysql_client,
                    const std::shared_ptr<sw::redis::Redis> &redis_client,
                    const std::shared_ptr<ServiceManager> &channel_manager,
                    const std::string &file_service_name)
            : es_manager_(std::make_shared<ESUserManager>(es_client)),
              redis_session_(std::make_shared<Session>(redis_client)),
              redis_LoginStatus_(std::make_shared<LoginStatus>(redis_client)),
              redis_LoginToken_(std::make_shared<LoginToken>(redis_client)),
              mysql_user_(std::make_shared<UserTable>(mysql_client)),
              file_service_name_(file_service_name),
              service_manager_(channel_manager) {
                es_manager_->create_user_index();
            }
        ~UserServiceImpl() override {}
        bool is_valid_nickname(const std::string &nickname) {
            int length = 0;
            for (size_t i = 0; i < nickname.size(); ) {
                unsigned char c = nickname[i];
                if (c <= 0x7F) { // ASCII字符（0~127，1字节UTF-8）
                    length += 1;
                    i += 1;
                } else if ((c & 0xE0) == 0xC0) { // 2字节UTF-8字符
                    // 0xE0 = 11100000（二进制），按位与后保留高3位
                    // 结果等于0xC0（11000000），说明是2字节UTF-8首字节
                    length += 2;
                    i += 2;
                } else if ((c & 0xF0) == 0xE0) { // 3字节UTF-8字符
                    // 0xF0 = 11110000，按位与后保留高4位
                    // 结果等于0xE0（11100000），说明是3字节UTF-8首字节（大部分汉字）
                    length += 3;
                    i += 3;
                } else if ((c & 0xF8) == 0xF0) { // 4字节UTF-8字符
                    // 0xF8 = 11111000，按位与后保留高5位
                    // 结果等于0xF0（11110000），说明是4字节UTF-8首字节（生僻字/特殊符号）
                    length += 4;
                    i += 4;
                } else {
                    return false; // 不符合UTF-8首字节规则，是非法编码
                }
            }
            return length >= 1 && length <= 30;
        }
        bool is_valid_password(const std::string &password) {
            if (password.length() < 6 || password.length() > 20) {
                return false;
            }
            for (char c : password) {
                if (!std::isalnum(c) && !std::ispunct(c)) {
                    return false; // 包含非字母、数字、特殊符号的字符
                }
            }
            return true;
        }
        virtual void UserRegister(::google::protobuf::RpcController* controller,
                       const ::MicroChat::UserRegisterReq* request,
                       ::MicroChat::UserRegisterRsp* response,
                       ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出昵称和密码
                std::string nickname = request->nickname();
                std::string password = request->password();
                if (nickname.empty() || password.empty()) {
                    err_response(request->request_id(), "用户名或密码不能为空");
                    LOG_WARN("用户注册失败，用户名或密码不能为空");
                    return;
                }
                //2. 检查昵称是否合法，长度限制，汉字占3个字符，总长度限制30个字符
                if (!is_valid_nickname(nickname)) {
                    err_response(request->request_id(), "昵称不合法，长度限制1-30个字符");
                    LOG_WARN("用户注册失败，昵称不合法：{}", nickname);
                    return;
                }
                //3. 检查密码是否合法，长度限制6-20个字符，只能包含字母、数字、标点符号特殊符号
                if(!is_valid_password(password)) {
                    err_response(request->request_id(), "密码不合法，长度限制6-20个字符");
                    LOG_WARN("用户注册失败，密码不合法");
                    return;
                }
                //4. 检查昵称是否已被注册
                auto existing_user = mysql_user_->select_by_nickname(nickname);
                if (existing_user) {
                    err_response(request->request_id(), "昵称已被注册");
                    LOG_WARN("用户注册失败，昵称已被注册：{}", nickname);
                    return;
                }
                //5. 向数据库中插入新用户记录
                std::string user_id = UUID();
                std::string password_hash = hashPassword(password);
                auto new_user = std::make_shared<User>(user_id, nickname, password_hash);
                bool insert_result = mysql_user_->insert(new_user);
                if (!insert_result) {
                    err_response(request->request_id(), "用户注册失败，数据库插入错误");
                    LOG_ERROR("用户注册失败，数据库插入错误：{}", nickname);
                    return;
                }
                //6. 向ES中添加用户索引记录
                bool es_result = es_manager_->appenduser(
                    user_id,
                    "",
                    nickname,
                    "",
                    "");
                if (!es_result) {
                    err_response(request->request_id(), "用户注册失败，ES索引错误");
                    LOG_ERROR("用户注册失败，ES索引错误：{}", nickname);
                    return;
                }
                //7. 返回成功响应
                response->set_request_id(request->request_id());
                response->set_success(true);
                response->set_user_id(user_id);
                LOG_INFO("用户注册成功：{}", nickname);
            }
        virtual void UserLogin(::google::protobuf::RpcController* controller,
                            const ::MicroChat::UserLoginReq* request,
                            ::MicroChat::UserLoginRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出昵称和密码
                string nickname = request->nickname();
                string password = request->password();
                if (nickname.empty() || password.empty()) {
                    err_response(request->request_id(), "用户名或密码不能为空");
                    LOG_WARN("用户登录失败，用户名或密码不能为空");
                    return;
                }
                //2. 根据昵称查询用户记录
                auto user = mysql_user_->select_by_nickname(nickname);
                if (!user) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("用户登录失败，用户不存在：{}", nickname);
                    return;
                }
                //3. 验证密码
                if (!verifyPassword(password, user->getPasswordHash())) {
                    err_response(request->request_id(), "密码错误");
                    LOG_WARN("用户登录失败，密码错误：{}", nickname);
                    return;
                }
                //4. 根据redis中是否已有登录状态，判断是否重复登录
                std::string login_status_key = user->getUserId();
                if (redis_LoginStatus_->is_login(login_status_key)) {
                    err_response(request->request_id(), "用户已登录，请勿重复登录");
                    LOG_WARN("用户登录失败，用户已登录：{}", nickname);
                    return;
                }
                //5. 生成session_id，存入redis，设置登录状态
                std::string session_id = UUID();
                redis_session_->set_session(session_id, user->getUserId());
                redis_LoginStatus_->adduser(login_status_key);
                //6. 返回成功响应，包含session_id和用户信息
                response->set_request_id(request->request_id());
                response->set_success(true);
                response->set_login_session_id(session_id);                response->set_user_id(user->getUserId());                LOG_INFO("用户登录成功：{}", nickname);
            }
        //以 1 开始，第二位 3~9 之间，后边 9 个数字字符
        bool is_valid_phone_number(const std::string& phone_number) {
            if (phone_number.length() != 11 || phone_number[0] != '1') {
                return false;
            }
            for (size_t i = 1; i < phone_number.length(); ++i) {
                if (!std::isdigit(phone_number[i])) {
                    return false;
                }
            }
            if (phone_number[1] < '3' || phone_number[1] > '9') {
                return false;
            }
            return true;
        }
        virtual void GetPhoneVerifyCode(::google::protobuf::RpcController* controller,
                            const ::MicroChat::PhoneVerifyCodeReq* request,
                            ::MicroChat::PhoneVerifyCodeRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出手机号
                std::string phone_number = request->phone_number();
                if (phone_number.empty()) {
                    err_response(request->request_id(), "手机号不能为空");
                    LOG_WARN("获取手机验证码失败，手机号不能为空");
                    return;
                }
                //2. 验证手机号格式是否合法
                if (!is_valid_phone_number(phone_number)) {
                    err_response(request->request_id(), "手机号格式不合法");
                    LOG_WARN("获取手机验证码失败，手机号格式不合法：{}", phone_number);
                    return;
                }
                //3. 生成6位数字验证码
                std::string verify_code = generateNumericCode(6);
                std::string token_id = UUID();
                //4. 将验证码存入redis，设置过期时间1分钟
                redis_LoginToken_->set_token(token_id, verify_code, 1 * 60 * 1000);
                //5. 返回成功响应，包含验证码（实际应用中应通过短信发送）
                response->set_request_id(request->request_id());
                response->set_success(true);
                response->set_verify_code_id(token_id);
                response->set_verify_code(verify_code);
                LOG_INFO("获取手机验证码成功，手机号：{}, 验证码：{}", phone_number, verify_code); 
            }
        virtual void PhoneRegister(::google::protobuf::RpcController* controller,
                            const ::MicroChat::PhoneRegisterReq* request,
                            ::MicroChat::PhoneRegisterRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出手机号、验证码ID、验证码
                std::string phone_number = request->phone_number();
                std::string verify_code_id = request->verify_code_id();
                std::string verify_code = request->verify_code();
                if (phone_number.empty() || verify_code_id.empty() || verify_code.empty()) {
                    err_response(request->request_id(), "手机号或验证码不能为空");
                    LOG_WARN("手机注册失败，手机号或验证码不能为空");
                    return;
                }
                //2. 验证手机号格式是否合法
                if (!is_valid_phone_number(phone_number)) {
                    err_response(request->request_id(), "手机号格式不合法");
                    LOG_WARN("手机注册失败，手机号格式不合法：{}", phone_number);
                    return;
                }
                //3. 从redis中取出验证码，验证是否匹配
                std::string stored_code;
                stored_code = redis_LoginToken_->get_token(verify_code_id);
                if (stored_code != verify_code) {
                    err_response(request->request_id(), "验证码错误或已过期");
                    LOG_WARN("手机注册失败，验证码错误或已过期，手机号：{}", phone_number);
                    return;
                }
                //4. 检查手机号是否已被注册
                auto existing_user = mysql_user_->select_by_phone(phone_number);
                if (existing_user) {
                    err_response(request->request_id(), "手机号已被注册");
                    LOG_WARN("手机注册失败，手机号已被注册：{}", phone_number);
                    return;
                }
                //5. 向数据库中插入新用户记录
                std::string user_id = UUID();
                auto new_user = std::make_shared<User>(user_id, phone_number);
                bool insert_result = mysql_user_->insert(new_user);
                if (!insert_result) {
                    err_response(request->request_id(), "手机注册失败，数据库插入错误");
                    LOG_ERROR("手机注册失败，数据库插入错误：{}", phone_number);
                    return;
                }
                //6. 向ES中添加用户索引记录,默认昵称为user_id
                bool es_result = es_manager_->appenduser(
                    user_id,
                    phone_number,
                    user_id,
                    "",
                    "");
                if (!es_result) {
                    err_response(request->request_id(), "手机注册失败，ES索引错误");
                    LOG_ERROR("手机注册失败，ES索引错误：{}", phone_number);
                    return;
                }
                //7. 返回成功响应
                response->set_request_id(request->request_id());
                response->set_success(true);
                response->set_user_id(user_id);
                LOG_INFO("手机注册成功：{}", phone_number);
            }
        virtual void PhoneLogin(::google::protobuf::RpcController* controller,
                            const ::MicroChat::PhoneLoginReq* request,
                            ::MicroChat::PhoneLoginRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出手机号、验证码ID、验证码
                std::string phone_number = request->phone_number();
                std::string verify_code_id = request->verify_code_id();
                std::string verify_code = request->verify_code();
                if (phone_number.empty() || verify_code_id.empty() || verify_code.empty()) {
                    err_response(request->request_id(), "手机号或验证码不能为空");
                    LOG_WARN("手机登录失败，手机号或验证码不能为空");
                    return;
                }
                //2. 验证手机号格式是否合法
                if (!is_valid_phone_number(phone_number)) {
                    err_response(request->request_id(), "手机号格式不合法");
                    LOG_WARN("手机登录失败，手机号格式不合法：{}", phone_number);
                    return;
                }
                //3. 从redis中取出验证码，验证是否匹配
                std::string stored_code;
                stored_code = redis_LoginToken_->get_token(verify_code_id);
                if (stored_code != verify_code) {
                    err_response(request->request_id(), "验证码错误或已过期");
                    LOG_WARN("手机登录失败，验证码错误或已过期，手机号：{}", phone_number);
                    return;
                }
                //4. 根据手机号查询用户记录
                auto user = mysql_user_->select_by_phone(phone_number);
                if (!user) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("手机登录失败，用户不存在：{}", phone_number);
                    return;
                }
                //5. 根据redis中是否已有登录状态，判断是否重复登录
                std::string login_status_key = user->getUserId();
                if (redis_LoginStatus_->is_login(login_status_key)) {   
                    err_response(request->request_id(), "用户已登录，请勿重复登录");
                    LOG_WARN("手机登录失败，用户已登录：{}", phone_number);
                    return;
                }
                //6. 生成session_id，存入redis，设置登录状态
                std::string session_id = UUID();
                redis_session_->set_session(session_id, user->getUserId());
                redis_LoginStatus_->adduser(login_status_key);
                //7. 返回成功响应，包含session_id和用户信息
                response->set_request_id(request->request_id());
                response->set_success(true);
                response->set_login_session_id(session_id);
                response->set_user_id(user->getUserId());
                LOG_INFO("手机登录成功：{}", phone_number);
            }
        virtual void GetUserInfo(::google::protobuf::RpcController* controller,
                            const ::MicroChat::GetUserInfoReq* request,
                            ::MicroChat::GetUserInfoRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出用户ID
                std::string user_id = request->user_id();
                if (user_id.empty() && request->has_session_id()) {
                    user_id = redis_session_->get_uid(request->session_id());
                }
                if (user_id.empty()) {
                    err_response(request->request_id(), "用户ID不能为空或会话已失效");
                    LOG_WARN("获取用户信息失败，用户ID不能为空");
                    return;
                }
                //2. 根据用户ID查询用户记录
                auto user = mysql_user_->select_by_id(user_id);
                if (!user) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("获取用户信息失败，用户不存在：{}", user_id);
                    return;
                }
                //3. 根据头像id，从文件服务获取头像数据
                std::string avatar_data;
                if (!user->getAvatarId().empty()) {
                    auto file_channel = service_manager_->get_service_node(file_service_name_);
                    if (file_channel) {
                        MicroChat::FileService_Stub file_stub(file_channel.get());
                        MicroChat::GetSingleFileReq file_req;
                        MicroChat::GetSingleFileRsp file_rsp;
                        file_req.set_file_id(user->getAvatarId());
                        file_req.set_request_id(request->request_id());
                        brpc::Controller cntl;
                        file_stub.GetSingleFile(&cntl, &file_req, &file_rsp, nullptr);
                        if (!cntl.Failed() && file_rsp.success()) {
                            avatar_data = file_rsp.file_data().file_content();
                        } else {
                            LOG_WARN("获取用户头像失败，文件服务响应错误，用户ID：{}", user_id);
                        }
                    } else {
                        LOG_WARN("获取用户头像失败，无法获取文件服务信道-{}，用户ID：{}", file_service_name_, user_id);
                    }
                }
                //4. 返回成功响应，包含用户信息
                response->set_request_id(request->request_id());
                response->set_success(true);
                MicroChat::UserInfo* user_info = response->mutable_user_info();
                user_info->set_user_id(user->getUserId());
                user_info->set_nickname(user->getNickname());
                user_info->set_phone(user->getPhone());
                user_info->set_description(user->getPersonalSignature());
                if (!avatar_data.empty()) {
                    user_info->set_avatar(avatar_data);
                }
                LOG_INFO("获取用户信息成功：{}", user_id);
            }
        virtual void GetMultiUserInfo(::google::protobuf::RpcController* controller,
                            const ::MicroChat::GetMultiUserInfoReq* request,
                            ::MicroChat::GetMultiUserInfoRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出用户ID列表
                if (request->users_id_size() == 0) {
                    err_response(request->request_id(), "用户ID列表不能为空");
                    LOG_WARN("获取多用户信息失败，用户ID列表不能为空");
                    return;
                }
                std::vector<std::string> user_ids;
                for (const auto& uid : request->users_id()) {
                    user_ids.push_back(uid);
                }
                //2. 根据用户ID列表查询用户记录
                auto users = mysql_user_->select_multi_users(user_ids);
                if (users.empty()) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("获取多用户信息失败，用户不存在");
                    return;
                }
                //3. 检查users数量是否与请求的ID列表数量一致
                if (users.size() != user_ids.size()) {
                    err_response(request->request_id(), "部分用户不存在");
                    LOG_WARN("获取多用户信息失败，部分用户不存在");
                    return;
                }
                //4. 批量获取用户头像数据
                auto file_channel = service_manager_->get_service_node(file_service_name_);
                std::map<std::string, MicroChat::FileDownloadData> file_data_map;
                if (!file_channel) {
                    LOG_WARN("获取多用户头像失败，无法获取文件服务信道-{}", file_service_name_);
                } else {
                    MicroChat::FileService_Stub file_stub(file_channel.get());
                    GetMultiFileReq file_req;
                    GetMultiFileRsp file_rsp;
                    file_req.set_request_id(request->request_id());
                    bool has_avatar = false;
                    for (const auto& user : users) {
                        if (!user.getAvatarId().empty()) {
                            file_req.add_file_id_list(user.getAvatarId());
                            has_avatar = true;
                        }
                    }
                    if (has_avatar) {
                        brpc::Controller cntl;
                        file_stub.GetMultiFile(&cntl, &file_req, &file_rsp, nullptr);
                        if (cntl.Failed() || !file_rsp.success()) {
                            LOG_WARN("获取多用户头像失败，文件服务响应错误");
                        } else {
                            for (auto const& [key, val] : file_rsp.file_data()) {
                                file_data_map[key] = val;
                            }
                        }
                    }
                }
                
                //5. 返回成功响应，包含用户信息列表
                for(auto& user : users) {
                    auto user_map = response->mutable_users_info();
                    UserInfo user_info;
                    user_info.set_user_id(user.getUserId());
                    user_info.set_nickname(user.getNickname());
                    user_info.set_description(user.getPersonalSignature());
                    user_info.set_phone(user.getPhone());
                    if (!user.getAvatarId().empty()) {
                        auto it = file_data_map.find(user.getAvatarId());
                        if (it != file_data_map.end()) {
                            user_info.set_avatar(it->second.file_content());
                        }
                    }
                    (*user_map)[user.getUserId()] = user_info;
                }
                response->set_request_id(request->request_id());
                response->set_success(true);
                LOG_INFO("获取多用户信息成功，用户数量：{}", users.size());
            }
        
        virtual void SetUserAvatar(::google::protobuf::RpcController* controller,
                            const ::MicroChat::SetUserAvatarReq* request,
                            ::MicroChat::SetUserAvatarRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出用户ID和头像数据
                std::string user_id = request->user_id();
                if (user_id.empty() && request->has_session_id()) {
                    user_id = redis_session_->get_uid(request->session_id());
                }
                std::string avatar_data = request->avatar();
                if (user_id.empty() || avatar_data.empty()) {
                    err_response(request->request_id(), "用户ID或头像数据不能为空");
                    LOG_WARN("设置用户头像失败，用户ID或头像数据不能为空");
                    return;
                }
                //2. 根据用户ID查询用户记录
                auto user = mysql_user_->select_by_id(user_id);
                if (!user) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("设置用户头像失败，用户不存在：{}", user_id);
                    return;
                }
                //3. 调用文件服务上传头像数据，获取头像文件ID
                auto file_channel = service_manager_->get_service_node(file_service_name_);
                if (!file_channel) {
                    err_response(request->request_id(), "无法获取文件服务信道");
                    LOG_WARN("设置用户头像失败，无法获取文件服务信道-{}，用户ID：{}", file_service_name_, user_id);
                    return;
                }
                MicroChat::FileService_Stub file_stub(file_channel.get());
                MicroChat::PutSingleFileReq file_req;
                MicroChat::PutSingleFileRsp file_rsp;
                file_req.set_request_id(request->request_id());
                file_req.mutable_file_data()->set_file_content(avatar_data);
                file_req.mutable_file_data()->set_file_name(user_id + "_avatar");
                file_req.mutable_file_data()->set_file_size(avatar_data.size());
                brpc::Controller cntl;
                file_stub.PutSingleFile(&cntl, &file_req, &file_rsp, nullptr);
                if (cntl.Failed() || !file_rsp.success()) {
                    err_response(request->request_id(), "上传头像失败，文件服务响应错误");
                    LOG_WARN("设置用户头像失败，上传头像失败，文件服务响应错误，用户ID：{}", user_id);
                    return;
                }
                std::string avatar_file_id = file_rsp.file_info().file_id();
                //4. 更新用户记录中的头像ID字段
                user->setAvatarId(avatar_file_id);
                bool update_result = mysql_user_->update(user);
                if (!update_result) {
                    err_response(request->request_id(), "设置用户头像失败，数据库更新错误");
                    LOG_ERROR("设置用户头像失败，数据库更新错误，用户ID：{}", user_id);
                    return;
                }
                //5. 更新ES中的用户头像ID索引
                bool es_result = es_manager_->appenduser(
                    user->getUserId(),
                    user->getPhone(),
                    user->getNickname(),
                    user->getPersonalSignature(),
                    avatar_file_id);
                if (!es_result) {
                    err_response(request->request_id(), "设置用户头像失败，ES索引错误");
                    LOG_ERROR("设置用户头像失败，ES索引错误，用户ID：{}", user_id);
                    return;
                }
                //6. 返回成功响应
                response->set_request_id(request->request_id());
                response->set_success(true);
                LOG_INFO("设置用户头像成功：{}", user_id);
            }
        virtual void SetUserNickname(::google::protobuf::RpcController* controller,
                            const ::MicroChat::SetUserNicknameReq* request,
                            ::MicroChat::SetUserNicknameRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出用户ID和新昵称
                std::string user_id = request->user_id();
                if (user_id.empty() && request->has_session_id()) {
                    user_id = redis_session_->get_uid(request->session_id());
                }
                std::string new_nickname = request->nickname();
                if (user_id.empty() || new_nickname.empty()) {
                    err_response(request->request_id(), "用户ID或新昵称不能为空");
                    LOG_WARN("设置用户昵称失败，用户ID或新昵称不能为空");
                    return;
                }
                //2. 根据用户ID查询用户记录
                auto user = mysql_user_->select_by_id(user_id);
                if (!user) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("设置用户昵称失败，用户不存在：{}", user_id);
                    return;
                }
                //3. 检查新昵称是否合法
                if (!is_valid_nickname(new_nickname)) {
                    err_response(request->request_id(), "新昵称不合法，长度限制1-30个字符");
                    LOG_WARN("设置用户昵称失败，新昵称不合法：{}", new_nickname);
                    return;
                }
                //4. 检查新昵称是否已被注册
                auto existing_user = mysql_user_->select_by_nickname(new_nickname);
                if (existing_user) {
                    err_response(request->request_id(), "新昵称已被注册");
                    LOG_WARN("设置用户昵称失败，新昵称已被注册：{}", new_nickname);
                    return;
                }
                //5. 更新用户记录中的昵称字段
                user->setNickname(new_nickname);
                bool update_result = mysql_user_->update(user);
                if (!update_result) {
                    err_response(request->request_id(), "设置用户昵称失败，数据库更新错误");
                    LOG_ERROR("设置用户昵称失败，数据库更新错误，用户ID：{}", user_id);
                    return;
                }
                //6. 更新ES中的用户昵称索引
                bool es_result = es_manager_->appenduser(
                    user->getUserId(),
                    user->getPhone(),
                    new_nickname,
                    user->getPersonalSignature(),
                    user->getAvatarId());
                if (!es_result) {
                    err_response(request->request_id(), "设置用户昵称失败，ES索引错误");
                    LOG_ERROR("设置用户昵称失败，ES索引错误，用户ID：{}", user_id);
                    return;
                }
                //7. 返回成功响应
                response->set_request_id(request->request_id());
                response->set_success(true);
                LOG_INFO("设置用户昵称成功：{}", user_id);
            }
        virtual void SetUserDescription(::google::protobuf::RpcController* controller,
                            const ::MicroChat::SetUserDescriptionReq* request,
                            ::MicroChat::SetUserDescriptionRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出用户ID和新个性签名
                std::string user_id = request->user_id();
                if (user_id.empty() && request->has_session_id()) {
                    user_id = redis_session_->get_uid(request->session_id());
                }
                std::string new_description = request->description();
                if (user_id.empty() || new_description.empty()) {
                    err_response(request->request_id(), "用户ID或新个性签名不能为空");
                    LOG_WARN("设置用户个性签名失败，用户ID或新个性签名不能为空");
                    return;
                }
                //2. 根据用户ID查询用户记录
                auto user = mysql_user_->select_by_id(user_id);
                if (!user) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("设置用户个性签名失败，用户不存在：{}", user_id);
                    return;
                }
                //3. 更新用户记录中的个性签名字段
                user->setPersonalSignature(new_description);
                bool update_result = mysql_user_->update(user);
                if (!update_result) {
                    err_response(request->request_id(), "设置用户个性签名失败，数据库更新错误");
                    LOG_ERROR("设置用户个性签名失败，数据库更新错误，用户ID：{}", user_id);
                    return;
                }
                //4. 更新ES中的用户个性签名索引
                bool es_result = es_manager_->appenduser(
                    user->getUserId(),
                    user->getPhone(),
                    user->getNickname(),
                    new_description,
                    user->getAvatarId());
                if (!es_result) {
                    err_response(request->request_id(), "设置用户个性签名失败，ES索引错误");
                    LOG_ERROR("设置用户个性签名失败，ES索引错误，用户ID：{}", user_id);
                    return;
                }
                //5. 返回成功响应
                response->set_request_id(request->request_id());
                response->set_success(true);
                LOG_INFO("设置用户个性签名成功：{}", user_id);
            }
        virtual void SetUserPhoneNumber(::google::protobuf::RpcController* controller,
                            const ::MicroChat::SetUserPhoneNumberReq* request,
                            ::MicroChat::SetUserPhoneNumberRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                };
                //1. 从请求中取出用户ID和新手机号
                std::string user_id = request->user_id();
                if (user_id.empty() && request->has_session_id()) {
                    user_id = redis_session_->get_uid(request->session_id());
                }
                std::string new_phone_number = request->phone_number();
                if (user_id.empty() || new_phone_number.empty()) {
                    err_response(request->request_id(), "用户ID或新手机号不能为空");
                    LOG_WARN("设置用户手机号失败，用户ID或新手机号不能为空");
                    return;
                }
                //2. 验证新手机号格式是否合法
                if (!is_valid_phone_number(new_phone_number)) {
                    err_response(request->request_id(), "新手机号格式不合法");
                    LOG_WARN("设置用户手机号失败，新手机号格式不合法：{}", new_phone_number);
                    return;
                }
                //3. 根据用户ID查询用户记录
                auto user = mysql_user_->select_by_id(user_id);
                if (!user) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("设置用户手机号失败，用户不存在：{}", user_id);
                    return;
                }
                //4. 更新用户记录中的手机号字段
                user->setPhone(new_phone_number);
                bool update_result = mysql_user_->update(user);
                if (!update_result) {
                    err_response(request->request_id(), "设置用户手机号失败，数据库更新错误");
                    LOG_ERROR("设置用户手机号失败，数据库更新错误，用户ID：{}", user_id);
                    return;
                }
                //5. 更新ES中的用户手机号索引
                bool es_result = es_manager_->appenduser(
                    user->getUserId(),
                    new_phone_number,
                    user->getNickname(),    
                    user->getPersonalSignature(),
                    user->getAvatarId());
                if (!es_result) {
                    err_response(request->request_id(), "设置用户手机号失败，ES索引错误");
                    LOG_ERROR("设置用户手机号失败，ES索引错误，用户ID：{}", user_id);
                    return;
                }
                //6. 返回成功响应
                response->set_request_id(request->request_id());
                response->set_success(true);
                LOG_INFO("设置用户手机号成功：{}", user_id);
            }
        virtual void SetUserPassword(::google::protobuf::RpcController* controller,
                            const ::MicroChat::SetUserPasswordReq* request,
                            ::MicroChat::SetUserPasswordRsp* response,
                            ::google::protobuf::Closure* done) {
                brpc::ClosureGuard done_guard(done);
                //错误处理函数
                auto err_response = [this, response](const std::string &rid, 
                    const std::string &errmsg){
                    response->set_request_id(rid);
                    response->set_success(false);
                    response->set_errmsg(errmsg);
                }; 
                //1. 从请求中取出参数
                std::string user_id = request->user_id();
                if (user_id.empty() && request->has_session_id()) {
                    user_id = redis_session_->get_uid(request->session_id());
                }
                std::string old_password = request->old_password();
                std::string new_password = request->new_password();
                std::string confirm_password = request->confirm_password();
                if (user_id.empty() || old_password.empty() || new_password.empty() || confirm_password.empty()) {
                    err_response(request->request_id(), "用户ID或密码字段不能为空");
                    LOG_WARN("设置用户密码失败，参数缺失");
                    return;
                }
                //2. 检查新密码一致性
                if (new_password != confirm_password) {
                    err_response(request->request_id(), "两次输入的新密码不一致");
                    LOG_WARN("设置用户密码失败，新密码不一致，用户ID：{}", user_id);
                    return;
                }
                //3. 检查新密码合法性
                if (!is_valid_password(new_password)) {
                    err_response(request->request_id(), "新密码不合法，长度限制6-20个字符");
                    LOG_WARN("设置用户密码失败，新密码格式错误");
                    return;
                }
                //4. 获取用户信息并验证旧密码
                auto user = mysql_user_->select_by_id(user_id);
                if (!user) {
                    err_response(request->request_id(), "用户不存在");
                    LOG_WARN("设置用户密码失败，用户不存在：{}", user_id);
                    return;
                }
                if (!verifyPassword(old_password, user->getPasswordHash())) {
                    err_response(request->request_id(), "原密码错误");
                    LOG_WARN("设置用户密码失败，原密码错误，用户ID：{}", user_id);
                    return;
                }
                //5. 更新数据库中的密码
                user->setPasswordHash(hashPassword(new_password));
                bool update_res = mysql_user_->update(user);
                if (!update_res) {
                    err_response(request->request_id(), "数据库更新密码失败");
                    LOG_ERROR("设置用户密码失败，数据库更新错误，用户ID：{}", user_id);
                    return;
                }
                //6. 修改密码后清理登录信息
                redis_LoginStatus_->deleteuser(user_id);
                if (request->has_session_id()) {
                    redis_session_->delete_session(request->session_id());
                }
                //7. 返回成功
                response->set_request_id(request->request_id());
                response->set_success(true);
                LOG_INFO("设置用户密码成功，用户ID：{}", user_id);
            }
    private:
        std::shared_ptr<ESUserManager> es_manager_;
        std::shared_ptr<Session> redis_session_;
        std::shared_ptr<LoginStatus> redis_LoginStatus_;
        std::shared_ptr<LoginToken> redis_LoginToken_;
        std::shared_ptr<UserTable> mysql_user_;
        std::string file_service_name_; //关联文件服务名称
        std::shared_ptr<ServiceManager> service_manager_; //服务信道管理器
    };

    class UserServer {
    public:
        UserServer(const std::shared_ptr<elasticlient::Client> es_client,
                   const std::shared_ptr<odb::core::database> mysql_client,
                   const std::shared_ptr<sw::redis::Redis> redis_client,
                   const std::shared_ptr<EtcdClientfinder> etcd_client_finder,
                   const std::shared_ptr<EtcdClientRegistry> etcd_client_registry,
                   const std::shared_ptr<brpc::Server> brpc_server)
            : es_client_(es_client),
              mysql_client_(mysql_client),
              redis_client_(redis_client),
              etcd_client_finder_(etcd_client_finder),
              etcd_client_registry_(etcd_client_registry),
              brpc_server_(brpc_server) {}

        void start() {
            brpc_server_ ->RunUntilAskedToQuit();
        }
    private:
        std::shared_ptr<elasticlient::Client> es_client_;
        std::shared_ptr<odb::core::database> mysql_client_;
        std::shared_ptr<sw::redis::Redis> redis_client_;
        std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
        std::shared_ptr<EtcdClientRegistry> etcd_client_registry_;
        std::shared_ptr<brpc::Server> brpc_server_;
    };

    class UserServerFactory {
    public:
        //构造redis客户端
        bool create_redis_client(
            const std::string &host,
            int port,
            int db,
            bool keep_alive) {
            sw::redis::ConnectionOptions connection_options;
            connection_options.host = host;
            connection_options.port = port;
            connection_options.db = db;
            connection_options.keep_alive = keep_alive;
            redis_client_ = std::make_shared<sw::redis::Redis>(connection_options);
            return redis_client_ != nullptr;
        }
        //构造mysql客户端
        bool create_mysql_client(
            const std::string& db_name,
            const std::string& user,
            const std::string& password,
            const std::string& host = "127.0.0.1",
            unsigned int port = 3306,
            const std::string &cset = "utf8",
            int conn_pool_count = 1) {
            mysql_client_ = MySQLDatabaseBuilder::createDatabase(
                db_name, user, password, host, port, cset, conn_pool_count);
            return mysql_client_ != nullptr;
        }
        //构造ES客户端
        bool create_es_client(const std::vector<std::string> &hosts) {
            es_client_ = ESClientBuilder::createClient(hosts);
            return es_client_ != nullptr;
        }
        //构造服务注册对象
        bool create_etcd_register_clients(
            const std::string &etcd_host,
            const std::string& service_name,
            const std::string& service_host) {
            etcd_client_registry_ = std::make_shared<EtcdClientRegistry>(etcd_host);
            if(!etcd_client_registry_) {
                LOG_ERROR("Failed to create EtcdClientRegistry");
                return false;
            }
            auto ret = etcd_client_registry_->register_service(
                service_name,
                service_host);
            return ret;
        }
        //构造服务发现对象与服务信道管理器
        bool create_etcd_finder_clients(
            const std::string &etcd_host,
            const std::string& base_service_name,
            const std::string& file_service_name) {
            file_service_name_ = file_service_name;
            service_manager_ = std::make_shared<ServiceManager>();
            if (!service_manager_) {
                LOG_ERROR("Failed to create ServiceManager");
                return false;
            }
            auto sm = service_manager_;
            auto put_cb = [sm](const std::string& instance, const std::string& addr) {
                sm->on_service_online(instance, addr);
            };
            auto del_cb = [sm](const std::string& instance, const std::string& addr) {
                sm->on_service_offline(instance, addr);
            };
            etcd_client_finder_ = std::make_shared<EtcdClientfinder>(etcd_host , base_service_name , put_cb, del_cb);
            return etcd_client_finder_ != nullptr;
        }
        // 构造rpc服务器
        bool create_brpc_server(uint16_t port , int timeout , int thread_num) {
            if(!es_client_ || !mysql_client_ || !redis_client_ || 
               !etcd_client_finder_ || !etcd_client_registry_) {
                LOG_ERROR("Failed to create brpc server, dependencies are not satisfied");
                return false;
            }
            brpc_server_ = std::make_shared<brpc::Server>();
            UserServiceImpl* user_service_impl = new UserServiceImpl(
                es_client_,
                mysql_client_,
                redis_client_,
                service_manager_,
                file_service_name_);
            if (brpc_server_->AddService(user_service_impl, brpc::SERVER_OWNS_SERVICE) != 0) {
                LOG_ERROR("Failed to add UserService to brpc server");
                return false;
            }
            brpc::ServerOptions options;
            options.idle_timeout_sec = timeout;
            options.num_threads = thread_num;
            if (brpc_server_->Start(port, &options) != 0) {
                LOG_ERROR("Failed to start brpc server on port {}", port);
                return false;
            }
            return true;
        }
        //构造用户服务器
        std::shared_ptr<UserServer> create_user_server() {
            if(!es_client_ || !mysql_client_ || !redis_client_ ||
               !etcd_client_finder_ || !etcd_client_registry_ || !brpc_server_) {
                LOG_ERROR("Failed to create UserServer, dependencies are not satisfied");
                return nullptr;
            }
            return std::make_shared<UserServer>(
                es_client_,
                mysql_client_,
                redis_client_,
                etcd_client_finder_,
                etcd_client_registry_,
                brpc_server_);
        }
    private:
        std::shared_ptr<elasticlient::Client> es_client_;
        std::shared_ptr<odb::core::database> mysql_client_;
        std::shared_ptr<sw::redis::Redis> redis_client_;
        std::shared_ptr<ServiceManager> service_manager_;
        std::shared_ptr<EtcdClientfinder> etcd_client_finder_;
        std::shared_ptr<EtcdClientRegistry> etcd_client_registry_;
        std::shared_ptr<brpc::Server> brpc_server_;
        std::string file_service_name_;
    };
} // namespace MicroChat
