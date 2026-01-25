#pragma once
#include <fstream>
#include <iostream>
#include <string>
#include <random>
#include <iomanip>
#include <atomic>
#include <sstream>
#include <openssl/sha.h>
#include "spdlog.hpp"

namespace MicroChat {

    ////生成由16位随机字符组成的字符串作为唯一ID
    std::string UUID() {
        // 1. 生成6个0~255之间的随机数字--生成12位16进制字符
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(0, 255);
        std::stringstream ss;
        for (int i = 0; i < 6; ++i) {
            int num = dis(gen);
            ss << std::hex << std::setw(2) << std::setfill('0') << num;
        }
        // 2. 通过一个静态变量生成一个2字节的编号数字--生成4位16进制数字字符
        static std::atomic<uint16_t> counter{0};
        uint16_t count = counter.fetch_add(1);
        ss << std::hex << std::setw(4) << std::setfill('0') << count;
        return ss.str();
    }

    // 辅助函数：生成随机盐值 (使用16进制字符以保证兼容性)
    std::string generateRandomSalt(size_t length) {
        static const char hex_chars[] = "0123456789abcdef";
        std::string salt;
        salt.resize(length);
        // 生成安全随机数
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        for (size_t i = 0; i < length; ++i) {
            salt[i] = hex_chars[dis(gen)];
        }
        return salt;
    }

    // SHA256 哈希函数
    std::string sha256(const std::string& data) {
        // 1. 定义存储SHA256二进制结果的数组，32字节
        unsigned char hash[SHA256_DIGEST_LENGTH];
        // 2. 定义SHA256运算的上下文结构体，保存哈希运算的中间状态
        SHA256_CTX sha256;
        // 3. 初始化SHA256上下文
        SHA256_Init(&sha256);
        // 4. 传入要哈希的数据
        // 参数1：初始化后的上下文；参数2：要哈希的字节数据（C字符串）；参数3：数据长度
        SHA256_Update(&sha256, data.c_str(), data.size());
        // 5. 完成哈希计算，将最终的二进制结果写入hash数组
        SHA256_Final(hash, &sha256);
        // 6. 将32字节的二进制结果转换成64位的十六进制字符串
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

    // hashPassword 函数
    // 返回值：盐值 + 密码 hash（用分隔符分隔，":"）
    std::string hashPassword(const std::string& password) {
        // 生成随机盐值（16字节）
        std::string salt = generateRandomSalt(16); 
        // 密码 + 盐值 拼接
        std::string salted_password = password + salt;
        // 做 hash 运算
        std::string password_hash = sha256(salted_password);
        // 返回 盐值:hash
        return salt + ":" + password_hash;
    }

    // 登录验证函数
    bool verifyPassword(const std::string& input_password, const std::string& stored_hash_with_salt) {
        // 解析存储的 盐值:hash
        size_t sep_pos = stored_hash_with_salt.find(":");
        if (sep_pos == std::string::npos) return false; // 格式错误
        std::string salt = stored_hash_with_salt.substr(0, sep_pos);
        std::string stored_hash = stored_hash_with_salt.substr(sep_pos + 1);

        // 对输入密码加盐 hash
        std::string input_hash = sha256(input_password + salt);
        // 对比 hash值，普通比较即可
        return input_hash == stored_hash;
    }
    //生成随机数字验证码字符串
    std::string generateNumericCode(size_t length) {
        std::string code;
        code.resize(length);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 9);
        for (size_t i = 0; i < length; ++i) {
            code[i] = '0' + dis(gen);
        }
        return code;
    }
    //读取文件内容到字符串中
    bool readFile(const std::string &filename, std::string &body) {
        std::ifstream ifs(filename , std::ios::binary);
        if(!ifs.is_open()){
            LOG_ERROR("Failed to open file: {}", filename);
            return false;
        }
        //提前获取文件大小，避免多次扩容
        ifs.seekg(0, std::ios::end);
        size_t file_size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        body.resize(file_size);
        ifs.read(&body[0], file_size);
        if(ifs.good() == false){
            LOG_ERROR("Failed to read file: {}", filename);
            return false;
        }
        return true;
    }
    //写字符串内容到文件中
    bool writeFile(const std::string &filename, const std::string &body) {
        std::ofstream ofs(filename , std::ios::binary);
        if(!ofs.is_open()){
            LOG_ERROR("Failed to open file: {}", filename);
            return false;
        }
        ofs.write(body.c_str(), body.size());
        if(ofs.good() == false){
            LOG_ERROR("Failed to write file: {}", filename);
            return false;
        }
        return true;
    }
}  // namespace MicroChat