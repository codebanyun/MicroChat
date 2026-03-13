# MicroChat Backend (Server)

本项目是 MicroChat 微服务架构的后端实现，采用 C++ 编写，基于 **brpc**、**Protobuf** 以及多种现代中间件构建。系统支持高并发、组件化部署，具备完善的用户管理、即时通讯、文件存储及语音识别等功能。

## 🏗️ 架构概览

MicroChat 后端采用典型的 **微服务架构**，通过 **Etcd** 实现服务注册与发现，利用 **Gateway** 统一处理客户端连接。

### 核心组件
- **网关服务 (Gateway Server)**:
  - 提供 HTTP 和 WebSocket 双协议支持。
  - 负责用户鉴权（Session/Token）、请求路由以及即时消息推送。
  - 作为系统入口，通过 Etcd 发现后端服务并进行负载均衡。

- **用户服务 (User Server)**:
  - 负责用户注册、登录、信息管理。
  - 集成 **Elasticsearch** 支持高性能用户搜索。
  - 使用 **MySQL (ODB)** 持久化存储用户信息。
  - 使用 **Redis** 存储会话和验证码。

- **好友服务 (Friend Server)**:
  - 管理好友关系、好友申请及其处理。
  - 负责聊天会话（Chat Session）的创建与成员管理。

- **消息服务 (Message Server)**:
  - **查询与异步投影**: 负责历史/最近消息查询、关键词检索，以及消费 MQ 事件后同步 ES 与媒体文件元信息。
  - **一致性协作**: 与 Forward 服务配合，基于 outbox、重试与死信机制实现最终一致。
  - **架构设计**: 如下图所示（以 Message Server 为例展示微服务内部结构）：
    ![Message Server 架构图](Image/message_server.png)

- **转发服务 (Forward Server)**:
  - **主事实写入入口**: 处理发送消息主链路，先将 `message + message_outbox` 通过 MySQL 本地事务提交。
  - **异步分发触发器**: 事务成功后投递 MQ；若失败由 outbox 补偿线程重试，不阻塞主链路可用性。


- **文件服务 (File Server)**:
  - 提供文件上传与下载功能，支持头像及群聊文件存储。

- **语音服务 (Speech Server)**:
  - 集成百度 AI C++ SDK，提供语音转文字（ASR）功能。

**总体架构如下**：
![整体架构.png](Image/整体架构.png)

## 🛠️ 技术栈

| 类别 | 技术 |
| :--- | :--- |
| **开发语言** | C++ 17 |
| **RPC 框架** | [brpc](https://github.com/apache/brpc) |
| **序列化** | Protobuf |
| **网络基础** | **自研高性能 HTTP 框架** (基于 Epoll Reactor) |
| **服务发现** | Etcd |
| **数据库** | MySQL 8.0 (使用 ODB ORM) |
| **缓存/会话** | Redis |
| **全文检索** | Elasticsearch |
| **消息队列** | RabbitMQ |
| **容器化** | Docker, Docker Compose |
| **日志** | spdlog |

## 🔒 消息一致性与可靠性策略

当前实现遵循“**核心事实强一致、外围副作用最终一致**”：

1. **主事实先提交（强一致）**
  - `Forward` 服务在处理 `NewMessage` 时，先将 `message` 与 `message_outbox` 在同一个 MySQL 本地事务中提交。
  - 只要事务提交成功，即认为消息主事实已成立。

2. **异步分发（最终一致）**
  - 事务成功后尝试立即投递 MQ；若投递失败，事件标记为 `FAILED`，由后台补偿线程重试。
  - 补偿线程周期扫描 `PENDING/FAILED` 事件，成功后置 `SUCCESS`，超过阈值置 `DEAD`。

3. **MQ 消费失败补偿（死信 + 定时重试）**
  - Message 服务消费失败时，不直接丢弃：
    - `retry_count < 3`：转入 Retry Queue（TTL 延时）
    - `retry_count >= 3`：转入 Dead Letter Queue
  - 通过消息头 `x-retry-count` 跟踪重试次数。

4. **搜索降级兜底**
  - 关键词检索优先走 Elasticsearch。
  - 若 ES 调用失败，自动降级到 MySQL 关键词查询，保证“可用性优先 + 数据一致”。

5. **媒体消息回写**
  - 媒体类消息先落主记录，再异步上传文件服务。
  - 上传成功后回写 `file_id`（及文件元信息）到 MySQL，避免“主消息存在但文件索引缺失”。

### 关键状态说明（Outbox）

- `PENDING`：待分发
- `PROCESSING`：处理中（预留状态）
- `SUCCESS`：分发成功
- `FAILED`：分发失败，等待下次重试
- `DEAD`：达到最大重试次数，进入人工/离线处理

## 🛠️ 自研 HTTP 框架说明

本项目基于 C++ 实现了一套高性能的 HTTP 框架，作为网关及各微服务的基础网络设施。

- **底层网络库 ([server.hpp](common/server.hpp))**:
  - 基于 **Epoll Reactor** 模型，通过多线程 EventLoop 实现高并发处理。
  - 封装了高效的 **Buffer** 管理机制、**TimerQueue** 定时任务及 **TCP Server** 基础组件。

- **HTTP 协议层 ([http.hpp](common/http.hpp))**:
  - 支持请求行、Header、Body 的状态机解析与路由分发（当前网关主要使用 `POST/OPTIONS`）。
  - 提供 `Request` / `Response` 类封装，自动化 Header 解析及正文处理。
  - 支持 `Keep-Alive` 长连接，适配现代浏览器及微服务间通信需求。

## 📁 目录结构

```text
server/
├── common/             # 通用封装库（自研 HTTP, MySQL, Redis, Etcd, RabbitMQ, ES 等）
├── proto/              # Protobuf 定义文件
├── gateway/            # 网关微服务
├── user/               # 用户微服务 
├── friend/             # 好友/会话微服务
├── message/            # 消息查询/检索与异步投影服务
├── forward/            # 消息主链路写入与异步分发触发服务
├── file/               # 文件上传下载服务
├── Image/              # 项目架构图及相关资源
├── speech/             # 语音识别服务
├── conf/               # 各服务配置文件 (.conf)
├── sql/                # 数据库初始化脚本 (.sql)
├── data/               # 持久化数据挂载点
├── depends.sh          # 自动提取二进制依赖脚本
├── entrypoint.sh       # 容器启动健康检查脚本
└── docker-compose.yml  # 一键部署配置
```

## 🚀 部署说明

### 1. 环境准备
确保已安装：
- Docker & Docker Compose
- C++ 编译环境 (GCC/Clang, CMake)

### 2. 编译项目
在 `server` 目录下执行：
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 3. 提取依赖
系统容器化部署需要各服务的动态库支持，执行脚本自动收集：
```bash
bash depends.sh
```

### 4. 启动服务 (Docker Compose)
在 `server` 目录下执行：
```bash
docker compose up -d
```
该命令将按顺序启动基础设施（Etcd, MySQL, Redis, ES, RabbitMQ）及所有微服务实例。

## 📝 开发注意事项
- **Proto 更新**: 修改 `proto/` 下的文件后，需重新生成源文件并重新编译受影响的服务。
- **配置文件**: 修改 `conf/` 下的配置需要重启对应容器使之生效。
- **数据持久化**: 所有的数据库、ES 索引及日志文件均挂载在 `data/` 目录下，部署前请确保该目录权限正确。
