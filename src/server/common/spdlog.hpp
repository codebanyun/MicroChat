#pragma once
#include <spdlog/spdlog.h>
#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>


namespace MicroChat {   
#define MAX_SIZE_FILE 4 * 1024 * 1024
#define MAX_NUM_FILE 50

// mode - 运行模式： true-发布模式； false调试模式

inline std::shared_ptr<spdlog::logger> g_default_logger;
void init_logger(bool mode , const std::string& file  = "./default.log", int32_t level = spdlog::level::info)
{
    // 如果全局注册表已有 same-name logger，直接复用，避免重复创建或抛异常
    auto existing = spdlog::get("default-logger");
    if (existing) {
        g_default_logger = existing;
        return;
    }
    if(mode == false) // 调试模式，默认在终端打印信息
    {
        g_default_logger = spdlog::stdout_color_mt("default-logger");
        g_default_logger -> set_level(spdlog::level::level_enum::trace);
        g_default_logger -> flush_on(spdlog::level::level_enum::trace);
        g_default_logger->set_pattern("[%n][%H:%M:%S][%t][%-8l]%v");
    }
    else // 发布模式，日志记录磁盘
    {
        // 1. 初始化异步日志线程池（全局初始化1次）
        static bool thread_pool_inited = false;
        if (!thread_pool_inited)
        {
            size_t queue_size = 8192;  // 日志队列容量（根据日志量调整）
            size_t thread_count = 1;   // 工作线程数
            spdlog::init_thread_pool(queue_size, thread_count);
            thread_pool_inited = true; // 避免重复初始化
        }
        // 2. 创建异步文件日志器
        // async_basic_logger_mt：异步多线程日志器，绑定全局线程池
        g_default_logger = spdlog::rotating_logger_mt<spdlog::async_factory>(
            "default-logger",  // 日志器名称
            file,              // 日志文件路径
            MAX_SIZE_FILE,     //每个文件最大容量
            MAX_NUM_FILE       //文件上限
       );
        // 3. 日志级别和刷新策略
        g_default_logger->set_level((spdlog::level::level_enum)level);
        // 发布模式建议：flush_on(warn/error) + 定时自动flush（避免日志堆积）
        g_default_logger->flush_on(spdlog::level::warn); // 警告及以上级别立即刷新（确保关键日志不丢失）
        // 配置每5秒自动刷新一次所有日志器的缓冲区
        // spdlog::flush_every(std::chrono::seconds(5));
        // 4. 配置日志格式
        g_default_logger->set_pattern("[%n][%Y-%m-%d %H:%M:%S][%t][%-8l]%v");
    }
}

#define LOG_TRACE(format, ...) g_default_logger->trace(std::string("[{}:{}] ") + format, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) g_default_logger->debug(std::string("[{}:{}] ") + format, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(format, ...) g_default_logger->info(std::string("[{}:{}] ") + format, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(format, ...) g_default_logger->warn(std::string("[{}:{}] ") + format, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) g_default_logger->error(std::string("[{}:{}] ") + format, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) g_default_logger->critical(std::string("[{}:{}] ") + format, __FILE__, __LINE__, ##__VA_ARGS__)

//日志关闭函数：程序退出前调用，确保资源释放和日志不丢失
void shutdown_logger()
{
    // 1. 先刷新所有未写入的日志（强制将缓冲区/队列中的日志刷到磁盘）
    if (g_default_logger)
    {
        g_default_logger->flush(); 
    }
    // 2. 销毁所有日志器（释放文件句柄、日志器内存）
    spdlog::drop_all();

    // 3. 关闭异步日志线程池（仅发布模式需要）
    // spdlog::shutdown() 会自动触发 drop_all()
    spdlog::shutdown();

    // 4. 重置全局日志器指针
    g_default_logger.reset();

    std::cout << "日志系统已安全关闭" << std::endl;
}
// inline void shutdown_logger()
// {
//     if (g_default_logger)
//     {
//         try { g_default_logger->flush(); } catch(...) {}
//         // 仅丢弃本模块创建的 logger（按 name），不影响其它 logger 或全局线程池
//         try { spdlog::drop("default-logger"); } catch(...) {}
//         g_default_logger.reset();
//     }
//     std::cout << "模块级日志器已安全关闭（未触及全局日志系统）" << std::endl;
// }

} // namespace MicroChat