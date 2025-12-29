#pragma once
#include <fstream>
#include <iostream>
#include <string>
#include <random>
#include <iomanip>
#include <atomic>
#include <sstream>
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