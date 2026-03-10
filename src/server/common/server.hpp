#pragma once
#include <iostream>
#include <vector>
#include <cstdint>
#include <assert.h>
#include <string>
#include <cstring>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <thread>
#include <sstream>
#include <functional>
#include <sys/epoll.h>
#include <unordered_map>
#include <memory>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <mutex>
#include <condition_variable>
#include <signal.h>
#include <errno.h>
#include <algorithm>
#include <typeinfo>
#include <utility>

enum class LogLevel : int {  // 底层类型指定为int，支持比较运算符（<、==）
    INF = 0,  
    DBG = 1,  
    ERR = 2   
};
constexpr LogLevel LOG_LEVEL = LogLevel:: DBG;

// 将std::thread::id转换为std::string
inline std::string ThreadIdToString(const std::thread::id& tid) 
{
    std::ostringstream oss;
    oss << tid;  
    return oss.str();
}

#define LOG(level , format, ...) do{\
        if(level < LOG_LEVEL) break;\
        time_t t = time(NULL);\
        struct tm* ltm = localtime(&t);\
        char tmp[32] = {0};\
        strftime(tmp , 31, "%H:%M:%S", ltm);\
        std::string tid_str = ThreadIdToString(std::this_thread::get_id()); \
        fprintf(stdout , "[%s %s %s:%d]" format  "\n" , tid_str.c_str(),  tmp ,__FILE__ , __LINE__ , ##__VA_ARGS__);\
    }while(0)

#define INF_LOG(format, ...) LOG(LogLevel::INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(LogLevel::DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(LogLevel::ERR, format, ##__VA_ARGS__)


#define BUFFER_DEFAULT_SIZE 1024
class Buffer
{
private:
    std::vector<char> _buffer; //buffer内存管理
    uint64_t _read_idx ; // 读偏移
    uint64_t _write_idx; // 写偏移
public:
    Buffer():_read_idx(0), _write_idx(0),_buffer(BUFFER_DEFAULT_SIZE) {}
    char* Begin(){return &(*_buffer.begin());} 
    // 获取当前写入起始地址
    char* CurWritePos(){return Begin() + _write_idx;}
    // 获取当前读取起始地址
    char* CurReadPos(){return Begin() + _read_idx ; }
    //获取缓冲区末尾空间大小
    uint64_t TailFreeSpace(){return _buffer.size() - _write_idx;}
    //获取缓冲区起始空间大小
    uint64_t HeadFreeSpace(){return _read_idx ;}
    //获取可读空间大小
    uint64_t ReadableSize(){return _write_idx - _read_idx; }
    // 读偏移向后移动len小大
    void MoveReadOffset(uint64_t len)
    {
        if(len == 0) return ;
        assert(len <= ReadableSize());
        _read_idx += len;
    }
    // 写偏移向后移动len大小
    void MoveWriteOffset(uint64_t len)
    {
        if(len == 0) return ;
        assert(len <= TailFreeSpace());
        _write_idx += len;
    }
    // 确保可写空间足够：1.末尾空间够返回 2.整体够，前移 3.直接扩容
    void EnsureWriteSpace(uint64_t len)
    {
        if(TailFreeSpace() >= len) return ;
        if(TailFreeSpace() + HeadFreeSpace() >= len)
        {
            auto size = ReadableSize();
            std::copy(CurReadPos() , CurWritePos() , Begin());
            _read_idx = 0 ;
            _write_idx = size;
        }
        else
        {
            DBG_LOG("RESIZE %ld", _write_idx + len);
            _buffer.resize(_write_idx + len);
        }
    }
    // 写入数据
    void Write(const void* data , uint64_t len)
    {
        if(len == 0) return ;
        EnsureWriteSpace(len);
        std::copy((const char*)data, (const char*)data + len ,  CurWritePos());
        MoveWriteOffset(len); 
    }
    // 写数据，接收string类型
    void WriteString(const std::string& str)
    {
        Write(str.c_str() , str.size());
    }
    void WriteBuffer(Buffer &data) {
        return Write(data.CurReadPos(), data.ReadableSize());
    }
    // 读取数据
    void Read(void* buf , uint64_t len)
    {
        assert(len <= ReadableSize());
        std::copy(CurReadPos() , CurReadPos() + len , (char*)buf);
        MoveReadOffset(len);
    }
    std::string ReadAsString(uint64_t len)
    {
        assert(len <= ReadableSize());
        std::string tmp;
        tmp.resize(len);
        Read(&tmp[0] , len);
        return tmp;
    }
    // 查找第一次出现回车符的位置
    char* FindCRLF()
    {
        char *res =(char*)memchr(CurReadPos() , '\n' , ReadableSize());
        return res;
    }
    std::string GetLine()
    {
        char* pos = FindCRLF();
        if(pos == NULL) return "";
        // 换行符也取出
        return ReadAsString(pos - CurReadPos() + 1);
    }
    void clear()
    {
        _read_idx = _write_idx = 0;
    }
};


#define MAX_LISTEN 1024
class Socket
{
private:
    int _sockfd ; 
public:
    Socket(int fd = -1):_sockfd(fd){}
    ~Socket(){ Close();}

    void Close()
    {
        if(_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }
    int Fd(){return _sockfd;}
    // 创建套接字
    bool Create()
    {
        _sockfd = socket(AF_INET , SOCK_STREAM , 0);
        if(_sockfd < 0)
        {
            ERR_LOG("CTEATE SOCKET FAILED");
            return false;
        }
        return true;
    }
    // 绑定ip port
    bool Bind(uint16_t port, const std::string& ip = "0.0.0.0")
    {   
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        int ret = bind(_sockfd , (const sockaddr*)&addr , sizeof(addr));
        if(ret < 0)
        {
            ERR_LOG("BIND ADDRESS FAILED");
            return false;
        }
        return true;
    }
    //开始监听
    bool Listen(int backlog = MAX_LISTEN)
    {
        int ret = listen(_sockfd , backlog);
        if(ret < 0 )
        {
            ERR_LOG("LSITEN FAILED");
            return false;
        }
        return true;
    }
    //发起连接请求
    bool Connect(const std::string& ip , uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        int ret = connect(_sockfd , (const sockaddr*)&addr , sizeof addr);
        if(ret < 0 )
        {
            ERR_LOG("CONNECT FAILED");
            return false;
        }
        return true;
    }
    //获取新连接
    int Accept()
    {
        int newfd = accept(_sockfd , nullptr , nullptr);
        if(newfd < 0)
        {
            ERR_LOG("ACCEPT FAILED");
            return -1;
        }
        return newfd;
    }
    // 接收数据
    ssize_t Recv(void* buf , size_t len , int flag = 0 )
    {
        ssize_t ret = recv(_sockfd , buf , len , flag);
        if (ret == 0) {
            return 0;
        }
        if (ret < 0) {
            //EAGAIN 当前socket的接收缓冲区中没有数据了，非阻塞的情况下出现
            //EINTR  表示当前socket的阻塞等待，被信号打断了，
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return -2; //表示本次非阻塞读取暂无数据
            }
            ERR_LOG("SOCKET RECV FAILED!!");
            return -1;
        }
        return ret; //实际接收的数据长度
    }
    ssize_t NonBlockRecv(void *buf, size_t len) 
    {
        return Recv(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前接收为非阻塞。
    }
    //发送数据
    ssize_t Send(const void* data , size_t len , int flag = 0)
    {
        ssize_t ret = send(_sockfd ,data , len , flag);
        if(ret < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return -2; 
            }
            ERR_LOG("SOCKET SEND FAILED!!");
            return -1;
        }
        return ret;
    }
    ssize_t NonBlockSend(void *buf, size_t len) 
    {
        if (len == 0) return 0;
        return Send(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前发送为非阻塞。
    }
    //创建一个客户端连接
    bool CreateClient(const std::string& ip , uint16_t port)
    {   
        if(Create() == false) return false;
        if(Connect(ip , port) == false) return false;
        return true; 
    }
    //创建一个服务端连接
    bool CreateServer( uint16_t port , const std::string& ip = "0.0.0.0" , bool block_flag = false)
    {
        if(Create() == false) return false;
        if(block_flag) NonBlock();
        ReuseAddress();
        if(Bind(port , ip) == false) return false;
        if(Listen() == false) return false;
        return true;
    }
    void ReuseAddress()
    {
        int val = 1; 
        setsockopt(_sockfd , SOL_SOCKET, SO_REUSEADDR , (void*)&val , sizeof(int));
        val =1 ;
        setsockopt(_sockfd , SOL_SOCKET , SO_REUSEPORT , (void*)&val ,sizeof(int));
    }
    void NonBlock()
    {
        int flag = fcntl(_sockfd , F_GETFL , 0);
        fcntl(_sockfd , F_SETFL , flag | O_NONBLOCK);
    }
};

class Poller;
class EventLoop;
class Channel
{
private:
    int _fd;
    EventLoop* _loop; //与eventloop关联
    uint32_t _events ; // 当前需要监控的事件
    uint32_t _revents ; //当前连接触发的事件
    using EventCallback = std::function<void()>;
    EventCallback _read_callback ; //可读事件回调
    EventCallback _write_callback ; //可写事件回调
    EventCallback _error_callback ; //错误事件回调
    EventCallback _close_callback ; //连接断开事件回调
    EventCallback _event_callback ; //任意事件回调
public:
    Channel(EventLoop* loop , int fd):_fd(fd) , _loop(loop), _events(0) , _revents(0){}
    int FD() const {return _fd;}
    uint32_t Events() { return _events; }//获取想要监控的事件
    void SetREvents(uint32_t events) { _revents = events; }//设置实际就绪的事件
    void SetReadCallback(const EventCallback &cb) { _read_callback = cb; }
    void SetWriteCallback(const EventCallback &cb) { _write_callback = cb; }
    void SetErrorCallback(const EventCallback &cb) { _error_callback = cb; }
    void SetCloseCallback(const EventCallback &cb) { _close_callback = cb; }
    void SetEventCallback(const EventCallback &cb) { _event_callback = cb; }
    //当前是否监控了可读
    bool ReadAble() { return (_events & EPOLLIN); } 
    //当前是否监控了可写
    bool WriteAble() { return (_events & EPOLLOUT); }
    //启动读事件监控
    void EnableRead() { _events |= EPOLLIN; Update(); }
    //启动写事件监控
    void EnableWrite() { _events |= EPOLLOUT; Update(); }
    //关闭读事件监控
    void DisableRead() { _events &= ~EPOLLIN; Update(); }
    //关闭写事件监控
    void DisableWrite() { _events &= ~EPOLLOUT; Update(); }
    //关闭所有事件监控
    void DisableAll() { _events = 0; Update(); }
    void Update();
    void Remove();
    //事件处理，一旦连接触发了事件，调用此函数
    void HandleEvent() 
    {
        // 读时间、对端关闭连接close 或 shutdown(SHUT_WR)、紧急事件
        if((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            if (_read_callback) _read_callback();
        }
        //有可能会释放连接的操作事件，一次只处理一个
         if (_revents & EPOLLOUT) {
            if (_write_callback) _write_callback();
        }else if (_revents & EPOLLERR) {
            if (_error_callback) _error_callback();
        }else if (_revents & EPOLLHUP) {
            if (_close_callback) _close_callback();
        }
        if (_event_callback) _event_callback();
    }
};


#define MAX_EPOLLEVENTS 1024
class Poller
{
private:
    int _epfd;
    struct epoll_event _evs[MAX_EPOLLEVENTS] ;
    std::unordered_map<int , Channel*> _channels;
private:
    bool HasChannel(const Channel* ch) const
    {  
        return _channels.find(ch->FD()) != _channels.end(); 
    }
    void Update(Channel* channel , int op)
    {
        int fd = channel->FD();
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = channel->Events();
        int ret = epoll_ctl(_epfd , op , fd, &ev);
        if(ret < 0 )
        {
            ERR_LOG("EPOLLCTL FAILED %s" , strerror(errno));
        }
    }
public:
    Poller()
    {
        _epfd = epoll_create(1);
        if(_epfd < 0 )
        {
            ERR_LOG("EPOLL CREATE FAILED");
            abort();
        }
    }
    ~Poller() {  // 析构时释放epoll fd
        if (_epfd >= 0) {
            close(_epfd);
        }
    }
    // 禁用拷贝/移动,避免多个对象管理同一epfd
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&&) = delete;
    Poller& operator=(Poller&&) = delete;

    //添加或修改事件监控
    void UpdateEvent(Channel* channel)
    {
        auto ret = HasChannel(channel);
        if(!ret)
        {
            //说明不存在，则添加
            _channels.insert({channel->FD() , channel});
            return Update(channel , EPOLL_CTL_ADD);
        }
        Update(channel , EPOLL_CTL_MOD);
    }
    //移除监控
    void RemoveEvent(Channel* channel)
    {
        auto it = _channels.find(channel->FD());
        if(it != _channels.end()) _channels.erase(it);
        Update(channel , EPOLL_CTL_DEL);
    }
    // 开始监控，返回活跃链接
    void Poll(std::vector<Channel*>& active)
    {
        int nfds = epoll_wait(_epfd , _evs , MAX_EPOLLEVENTS , -1);
        if(nfds < 0)
        {
            if(errno == EINTR) return ; // 信号中断，直接返回
            ERR_LOG("EPOLL WAIT ERROR %s" , strerror(errno));
            abort();
        }
        active.reserve(nfds);  // 预分配空间，减少vector扩容开销
        for(int i = 0 ; i< nfds ; i++)
        {
            auto it = _channels.find(_evs[i].data.fd);
            assert(it != _channels.end());
            it->second->SetREvents(_evs[i].events); // 设置就绪事件
            active.push_back(it->second);
        }
    }
};



using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
class TimerTask
{
private:
    u_int64_t _id ; //定时器任务id
    u_int32_t _timeout ; // 超时时间
    bool _canceled; //false表示不取消，true表示取消
    TaskFunc _taskcb ; //要执行的定时任务
    ReleaseFunc _release; //删除定时器对象信息
public:
    TimerTask(u_int64_t id , u_int32_t delay , const TaskFunc &cb ):
        _id(id) , _timeout(delay) , _canceled(false), _taskcb(cb){}
    ~TimerTask()
    {
        if(_canceled == false && _taskcb) _taskcb();
        if(_release) _release();
    }
    void Cancel() {_canceled = true;}
    void SetRelease(const ReleaseFunc& cb) {_release = cb;};
    u_int32_t DelayTime() {return _timeout;}
};

class TimerWheel
{
private:
    using PtrTask = std::shared_ptr<TimerTask>;
    using WeakTask = std::weak_ptr<TimerTask>;
    int _tick ; // 秒针，指向哪里就释放哪里，执行对应的任务
    int _capacity ; //表盘最大数量， 最大延迟时间
    std::vector<std::vector<PtrTask>> _wheel;
    std::unordered_map<u_int64_t , WeakTask> _timers;
    EventLoop* _loop ;
    int _timerfd ; // 定时器描述符，可读事件回调即读定时器，执行定时任务
    std::unique_ptr<Channel> _timer_channel;
private:
    void RemoveTimer(u_int64_t id)
    {
        auto it = _timers.find(id);
        if(it != _timers.end())
        {
            _timers.erase(it);
        }
    }
    static int CreateTimerfd()
    {
        int timefd = timerfd_create(CLOCK_MONOTONIC , 0);
        if(timefd < 0)
        {
            ERR_LOG("TIMERFD CREATE FAILED");
            abort();
        }
        struct itimerspec itime;
        itime.it_value.tv_sec =1 ;
        itime.it_value.tv_nsec = 0 ; //第一次超时时间为1s
        itime.it_interval.tv_sec = 1;
        itime.it_interval.tv_nsec = 0 ; //第一次超时后，每次超时的时间间隔都是1s
        timerfd_settime(timefd , 0 , &itime , nullptr);
        return timefd;
    }
    int ReadTimerfd()
    {
        uint64_t num ; 
        int ret = read(_timerfd , &num , 8);
        if(ret < 0)
        {
            // 非阻塞/被信号中断，视为无超时
            if (errno == EAGAIN || errno == EINTR) 
            { 
                return 0;
            }
            ERR_LOG("READ TIMERFD FAILED");
            abort();
        }
        return num;
    }
    void OnTime()
    {
        int num = ReadTimerfd();
        for(int i = 0 ; i < num ; ++i)
        {
            RunTimerTask();
        }
    }
    void RunTimerTask()
    {
        _tick = (_tick + 1)%_capacity;
        _wheel[_tick].clear(); //释放对应的shared_ptr
    }
    void TimerAddInloop(u_int64_t id , u_int32_t delay , const TaskFunc& cb)
    {
        PtrTask pt(new TimerTask(id , delay , cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer , this , id));
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
        _timers[id] = WeakTask(pt);
    }
    void TimerRefreshInloop(u_int64_t id)
    {
        auto it = _timers.find(id);
        if(it == _timers.end())
        {
            return ;
        }
        PtrTask pt = it->second.lock(); // 获取对象对应的shared_ptr
        auto delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }
    void TimerCancelInloop(u_int64_t id)
    {
        auto it = _timers.find(id);
        if(it == _timers.end())
        {
            return ;
        }
        PtrTask pt = it->second.lock();
        if(pt) pt->Cancel();
    }
public:
    TimerWheel(EventLoop *loop):_capacity(60) , _tick(0) , _wheel(_capacity) , _loop(loop),
        _timerfd(CreateTimerfd()) , _timer_channel(std::make_unique<Channel>(_loop, _timerfd))
    {
        _timer_channel->SetReadCallback(std::bind(&TimerWheel::OnTime , this));
        _timer_channel->EnableRead();
    }
    // 析构：释放timerfd资源
    ~TimerWheel() {
        if (_timerfd >= 0) {
            close(_timerfd);
        }
    }
    // 禁用拷贝/移动（避免多对象管理同一timerfd）
    TimerWheel(const TimerWheel&) = delete;
    TimerWheel& operator=(const TimerWheel&) = delete;
    TimerWheel(TimerWheel&&) = delete;
    TimerWheel& operator=(TimerWheel&&) = delete;
    void TimerAdd(u_int64_t id , u_int32_t delay , const TaskFunc& cb);

    void TimerRefresh(u_int64_t id);

    void TimerCancel(u_int64_t id);
    //线程不安全
    bool HasTimer(uint64_t id)
    {
        return _timers.find(id) == _timers.end() ? false : true;
    }
};

class EventLoop
{
private:
    using Functor = std::function<void()>;
    std::thread::id _thread_id; //线程ID
    int _eventfd ; //唤醒监控时可能导致的阻塞
    std::unique_ptr<Channel> _event_channel;
    Poller _poller; //进行所有描述符事件监控
    std::vector<Functor> _tasks; //任务池
    std::mutex _mutex ; 
    TimerWheel _timer_wheel; 
private:
    int CreateEventFd()
    {
        int fd = eventfd(0 , EFD_CLOEXEC | EFD_NONBLOCK);
        if(fd < 0)
        {
            ERR_LOG("EVENTFD CREATE FAILED");
            abort();
        }
        return fd;
    }
    //读取一下即可，置0
    void ReadEvent()
    {
        uint64_t num =  0 ;
        int ret = read(_eventfd , &num , sizeof num);
        if(ret < 0)
        {
            if(errno == EINTR || errno == EAGAIN) return ;
            ERR_LOG("READ EVENTFD FAILED");
            abort();
        }
    }
    void WeakUpEventfd()
    {
        uint64_t val = 1 ;
        int ret = write(_eventfd , &val , sizeof val);
        if(ret < 0)
        {
            if(errno == EINTR) return;
            ERR_LOG("WRITE EVENTFD FAILED");
            abort();
        }
    }
public:
    EventLoop():_thread_id(std::this_thread::get_id()) , 
                _eventfd(CreateEventFd()),
                _event_channel(std::make_unique<Channel>(this , _eventfd)),
                _timer_wheel(this)
                {
                    _event_channel->SetReadCallback([this](){ReadEvent();});
                    _event_channel->EnableRead();
                }
    void RunTasks()
    {
        std::vector<Functor> tasks;
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.swap(tasks);
        }
        for(auto& t : tasks) t();
    }
    void Start()
    {
        while(1)
        {
            //事件监控
            std::vector<Channel*> actives;
            _poller.Poll(actives);
            //事件处理 
            for(auto& ch : actives)
            {
                ch->HandleEvent();
            }
            //执行任务
            RunTasks();
        }
    }
    bool IsInloop()
    {
        return _thread_id == std::this_thread::get_id();
    }
    void RunInloop(const Functor& cb)
    {
        if(IsInloop()) return cb();
        QueueInloop(cb);
    }
    void AssertInloop()
    {
        assert(_thread_id == std::this_thread::get_id());
    }
    void QueueInloop(const Functor& cb)
    {
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.push_back(cb);
        }
        //给eventfd写入事件，触发可读，防止阻塞
        WeakUpEventfd();
    }
    //添加/修改描述符的事件监控
    void UpdateEvent(Channel *channel) { _poller.UpdateEvent(channel); }
    //移除描述符的监控
    void RemoveEvent(Channel *channel) { _poller.RemoveEvent(channel); }
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb) { _timer_wheel.TimerAdd(id, delay, cb); }
    void TimerRefresh(uint64_t id) { _timer_wheel.TimerRefresh(id); }
    void TimerCancel(uint64_t id) { _timer_wheel.TimerCancel(id); }
    bool HasTimer(uint64_t id) { return _timer_wheel.HasTimer(id); }

};


void Channel::Remove() { return _loop->RemoveEvent(this); }
void Channel::Update() { return _loop->UpdateEvent(this); }
void TimerWheel::TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb) {
    _loop->RunInloop(std::bind(&TimerWheel::TimerAddInloop, this, id, delay, cb));
}
//刷新/延迟定时任务
void TimerWheel::TimerRefresh(uint64_t id) {
    _loop->RunInloop(std::bind(&TimerWheel::TimerRefreshInloop, this, id));
}
void TimerWheel::TimerCancel(uint64_t id) {
    _loop->RunInloop(std::bind(&TimerWheel::TimerCancelInloop, this, id));
}


class LoopThread
{
private:
    EventLoop* _loop;
    std::thread _thread; //eventloop对应的线程
    std::mutex _mutex;
    std::condition_variable _cond ;
private:
    void ThreadEntry()
    {
        EventLoop loop;
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _loop = &loop;
            _cond.notify_all();
        }
        loop.Start();
    }
public:
    LoopThread():_loop(nullptr) , _thread(std::thread([this](){ThreadEntry();})){}
    ~LoopThread() {
        if (_thread.joinable()) {
            _thread.detach();
        }
    }
    auto Getloop() 
    {
        if(_loop) return _loop;
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _cond.wait(_lock , [&](){return _loop != nullptr;});
        }
        return _loop;
    }
};


class LoopThreadPool
{
private:
    int _thread_num;
    int _next_idx;
    EventLoop* _baseloop;
    std::vector<LoopThread*> _threads;
    std::vector<EventLoop*> _loops;
public:
    LoopThreadPool(EventLoop* loop):_thread_num(0) , _next_idx(0) , _baseloop(loop){}
    ~LoopThreadPool()
    {
        for (auto *thread : _threads)
        {
            delete thread;
        }
    }
    void SetThreadNum(int count){_thread_num = count;}
    void Create()
    {
        if(_thread_num > 0 )
        {
            _threads.resize(_thread_num);
            _loops.resize(_thread_num);
            for(int i = 0 ; i< _thread_num ; ++i)
            {
                _threads[i] = new LoopThread();
                _loops[i] = _threads[i]->Getloop();
            }
        }
    }
    auto NextLoop()
    {
        if(_thread_num == 0) return _baseloop;
        _next_idx = (_next_idx + 1)% _thread_num;
        return _loops[_next_idx];
    }
};

class Any
{
private:
    class Base
    {
    public:
        virtual ~Base(){}
        virtual const std::type_info& type() = 0 ;
        virtual Base* clone() = 0 ;
    };
    template<class T>
    class Holder : public Base
    {
    public:
        T _val ; 
    public:
        Holder(const T& val):_val(val){}
        virtual const std::type_info& type(){return typeid(T);}
        virtual Holder* clone() {return new Holder(_val);}
    };
    Base* _content;
public:
    Any():_content(nullptr){}
    template<class T>
    Any(const T &val):_content(new Holder<T>(val)){}
    Any(const Any& other):_content(other._content?other._content->clone() : nullptr){}
    Any& operator=(const Any& other)
    {
        Any(other).swap(*this);
        return *this;
    }
    // 移动构造
    Any(Any&& other) noexcept : _content(other._content) {
        other._content = nullptr;  // 转移资源所有权，避免被析构
    }

    // 移动赋值
    Any& operator=(Any&& other) noexcept {
        if (this != &other) {
            delete _content;       // 释放当前资源
            _content = other._content;  // 接管对方资源
            other._content = nullptr;
        }
        return *this;
    }

    // 移动版本的operator=(const T&)
    template<class T>
    Any& operator=(T&& val) {  // 支持右值赋值（如临时对象）
        Any(std::forward<T>(val)).swap(*this);
        return *this;
    }
    ~Any(){delete _content;}
    template<class T>
    T* get()
    {
        assert(typeid(T) == _content->type());
        return &((Holder<T>*)_content)->_val;
    }
    Any& swap(Any& other)
    {
        std::swap(_content , other._content);
        return *this;
    }
    template<class T>
    Any& operator=(const T& val)
    {
        Any(val).swap(*this);
        return *this;
    }
};


//DISCONECTED -- 连接关闭状态；   CONNECTING -- 连接建立成功-待处理状态
//CONNECTED -- 连接建立完成，各种设置已完成，可以通信的状态；  DISCONNECTING -- 待关闭状态
enum class ConnStatu
{
    DISCONECTED , CONNECTING , CONNECTED , DISCONNECTING
};
class Connection ;
using PtrConnection = std::shared_ptr<Connection>;
class Connection : public std::enable_shared_from_this<Connection>
{
private:
    uint64_t _conn_id ; // 连接id
    int _sockfd ; // 连接关联的文件描述符 
    EventLoop* _loop ; // 关联的eventloop
    ConnStatu _statu; //连接状态
    Socket _socket;  // 套接字管理
    Channel _channel  ; //事件管理
    Buffer _inbuffer ; //输入缓冲区————存放从sockfd中读取的数据
    Buffer _outbuffer ; //输出缓冲区————存放要送法的数据
    Any _context ; //请求处理的上下文
    bool _enable_inactive_release ; // 是否开启非活跃连接销毁

    using ConnectedCallback = std::function<void(const PtrConnection&)>;
    using MessageCallback = std::function<void(const PtrConnection&, Buffer *)>;
    using ClosedCallback = std::function<void(const PtrConnection&)>;
    using AnyEventCallback = std::function<void(const PtrConnection&)>;
    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    ClosedCallback _closed_callback;
    AnyEventCallback _event_callback;
    //组件内的连接关闭回调，服务器组件内会把所有的连接管理起来，一旦某个连接要关闭，则从管理的地方移除掉对应信息
    ClosedCallback _server_closed_callback;
private:
    void HandleRead()
    {
        //接收socket数据，放到缓冲区
        char buf[65535] = {0};
        ssize_t ret = _socket.NonBlockRecv(buf , 65535);
        if (ret == -2)
        {
            return;
        }
        // 1. 处理对端正常关闭（ret == 0）
        if (ret == 0) 
        {
            return ShutdownInloop(); // 对端关闭，触发连接关闭逻辑
        }
        if(ret < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return; // 暂时无数据，继续监听读事件
            //不直接关闭
            return ShutdownInloop();
        }
        _inbuffer.Write(buf , ret);
        //调用message_cal进行业务处理
        if(_inbuffer.ReadableSize() && _message_callback)
        {
            _message_callback(shared_from_this() , &_inbuffer);
        }
    }
    void HandleWrite()
    {
        //发送数据
        ssize_t ret = _socket.NonBlockSend(_outbuffer.CurReadPos(), _outbuffer.ReadableSize());
        if(ret == -2)
        {
            return;
        }
        if(ret < 0)
        {
            if(_inbuffer.ReadableSize() && _message_callback)
            {
                _message_callback(shared_from_this() , &_inbuffer);
            }
            return Release();
        }
        if (ret > 0)
        {
            _outbuffer.MoveReadOffset(static_cast<uint64_t>(ret));
        }
        if(_outbuffer.ReadableSize() == 0)
        {
            _channel.DisableWrite(); // 没有数据需要发送，关闭写事件监控
            if(_statu == ConnStatu::DISCONNECTING) return Release();
        }
    }
    void HandleClose()
    {
        if(_inbuffer.ReadableSize())
        {
            _message_callback(shared_from_this() , &_inbuffer);
        }
        Release();
    }
    void HandleError()
    {
        HandleClose();
    }
    void HandleEvent()
    {
        if(_enable_inactive_release) _loop->TimerRefresh(_conn_id);
        if(_event_callback) _event_callback(shared_from_this());
    }
    void ReleaseInloop()
    {
        if (_sockfd < 0) return; // 避免重复释放
        //修改连接状态
        _statu = ConnStatu::DISCONECTED;
        //移除事件监控
        _channel.Remove();
        //关闭描述符
        _socket.Close();
        _sockfd = -1;
        //如果定时器队列中还有定时销毁任务，则取消
        if(_loop->HasTimer(_conn_id)) CancalInactiveReleaseInloop();
        //调用关闭回调
        if(_closed_callback) _closed_callback(shared_from_this());
        //移除服务器内部管理
        if(_server_closed_callback) _server_closed_callback(shared_from_this());
    }
    void ShutdownInloop()
    {
        _statu = ConnStatu::DISCONNECTING;
        if(_inbuffer.ReadableSize())
            if(_message_callback)
                _message_callback(shared_from_this() , &_inbuffer);
        if(_outbuffer.ReadableSize())
            if(!_channel.WriteAble())
                _channel.EnableWrite();
        if(_outbuffer.ReadableSize() == 0) Release();
    }
    void CancalInactiveReleaseInloop()
    {
        _enable_inactive_release = false;
        if(_loop->HasTimer(_conn_id))
            _loop->TimerCancel(_conn_id);
    }
    void EstablishedInloop()
    {
        assert(_statu == ConnStatu::CONNECTING);
        _statu = ConnStatu::CONNECTED;
        _channel.EnableRead();
        if(_connected_callback) _connected_callback(shared_from_this());
    }
    void SendInloop(Buffer&& buf)
    {
        if(_statu == ConnStatu::DISCONECTED) return ;
        _outbuffer.WriteBuffer(buf);
        if(!_channel.WriteAble()) _channel.EnableWrite();
    }
    void EnableInactiveReleasedInloop(int sec)
    {
        _enable_inactive_release = true;
        if(_loop->HasTimer(_conn_id)) return _loop->TimerRefresh(_conn_id);
        _loop->TimerAdd(_conn_id , sec, std::bind(&Connection::Release , this));
    }
    void UpgradeInloop(const Any& context , const ConnectedCallback& conn , const MessageCallback& msg,
                        const ClosedCallback &closed, const AnyEventCallback &event)
    {
        _context = context;
        _connected_callback = conn;
        _message_callback = msg; 
        _closed_callback = closed;
        _event_callback = event;
    }
public: 
    Connection(EventLoop* loop  , uint64_t connid , int sockfd):
        _conn_id(connid) , _sockfd(sockfd) , _loop(loop) , _statu(ConnStatu::CONNECTING) , _socket(sockfd) , 
        _channel(loop , sockfd) , _enable_inactive_release(false)
    {
        _channel.SetCloseCallback(std::bind(&Connection::HandleClose, this));
        _channel.SetEventCallback(std::bind(&Connection::HandleEvent, this));
        _channel.SetReadCallback(std::bind(&Connection::HandleRead, this));
        _channel.SetWriteCallback(std::bind(&Connection::HandleWrite, this));
        _channel.SetErrorCallback(std::bind(&Connection::HandleError, this));
    }
    ~Connection(){DBG_LOG("REALEADE CONNECTION%p" , this);}
    int Fd() {return _sockfd ;}
    int Id(){return _conn_id;}
    bool Connected(){return _statu == ConnStatu::CONNECTED;}
    //设置上下文
    void SetContext(const Any& context){_context = context;}
    //获取上下文
    Any* GetContext(){return &_context;}
    void SetConnectedCallback(const ConnectedCallback&cb) { _connected_callback = cb; }
    void SetMessageCallback(const MessageCallback&cb) { _message_callback = cb; }
    void SetClosedCallback(const ClosedCallback&cb) { _closed_callback = cb; }
    void SetAnyEventCallback(const AnyEventCallback&cb) { _event_callback = cb; }
    void SetSrvClosedCallback(const ClosedCallback&cb) { _server_closed_callback = cb; }
    void Release()
    {
        _loop->QueueInloop([this](){ReleaseInloop();});
    }
    void Established()
    {
        _loop->RunInloop([this](){EstablishedInloop();});
    }
    void Send(const char* data , size_t len)
    {
        Buffer buf; 
        buf.Write(data , len);
        _loop->RunInloop([this, buf = std::move(buf)]() mutable{
            SendInloop(std::move(buf));
        });
    }
    //对外提供的关闭接口
    void Shutdown()
    {
        _loop->RunInloop([this](){ShutdownInloop();});
    }
    //取消非活跃连接
    void CancalInactiveRelease()
    {
        _loop->RunInloop([this](){CancalInactiveReleaseInloop();});
    }
    //启动非活跃连接
    void EnableInactiveRelease(int sec)
    {
        _loop->RunInloop([this, sec](){EnableInactiveReleasedInloop(sec);});
    }
    //切换协议---重置上下文以及阶段性回调处理函数 -- 须在EventLoop线程中立即执行
    //防备新的事件触发后，处理的时候，切换任务还没有被执行--会导致数据使用原协议处理了。
    void Upgrade(const Any& context , const ConnectedCallback& conn , const MessageCallback& msg,
                const ClosedCallback &closed, const AnyEventCallback &event)
    {
        _loop->AssertInloop();
        _loop->RunInloop(std::bind(&Connection::UpgradeInloop,this, context , conn, msg, closed , event));
    }
};

class Acceptor
{
private:
    Socket _socket ; // 创建监听套接字
    EventLoop* _loop ; 
    Channel _channel ; // 对监听套接字进行事件管理
    using AcceptCallback = std::function<void(int)>;
    AcceptCallback _accept_callback ; 
private:
    int CreateServer(int port)
    {
        bool ret = _socket.CreateServer(port);
        assert(ret);
        return _socket.Fd();
    }
    void HandleRead()
    {
        int newfd = _socket.Accept();
        if(newfd < 0) return;
        if(_accept_callback) _accept_callback(newfd);
    }
public :
    Acceptor(EventLoop* loop , int port):_socket(CreateServer(port)) , _loop(loop) , _channel(loop , _socket.Fd())
    {
        _channel.SetReadCallback([this](){HandleRead();});
    }
    void SetAcceptCallback(const AcceptCallback& cb){ _accept_callback = cb;}
    void Listen(){_channel.EnableRead();}
};

class TcpServer
{
private:
    uint64_t _id; //自增长连接id
    int _port ; 
    int _timeout ; //非活跃连接超时时间
    bool _enable_intactive_release  ;
    EventLoop _baseloop;  //主reactor ，负责监听新连接
    Acceptor _acceptor ; //监听套接字管理
    LoopThreadPool _pool ; //从reactor池
    std::unordered_map<uint64_t , PtrConnection> _conns ; 

    using ConnectedCallback = std::function<void(const PtrConnection&)>;
    using MessageCallback = std::function<void(const PtrConnection&, Buffer *)>;
    using ClosedCallback = std::function<void(const PtrConnection&)>;
    using AnyEventCallback = std::function<void(const PtrConnection&)>;
    using Functor = std::function<void()>;
    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    ClosedCallback _closed_callback;
    AnyEventCallback _event_callback;
private:
    void NewConnection(int fd)
    {
        ++_id;
        PtrConnection conn = std::make_shared<Connection>(_pool.NextLoop() , _id , fd);
        conn->SetMessageCallback(_message_callback);
        conn->SetClosedCallback(_closed_callback);
        conn->SetConnectedCallback(_connected_callback);
        conn->SetAnyEventCallback(_event_callback);
        conn->SetSrvClosedCallback([this](const PtrConnection& conn){RemoveConnection(conn);});
        if(_enable_intactive_release && _timeout > 0) conn->EnableInactiveRelease(_timeout);
        conn->Established();
        _conns.emplace(_id, conn);
    }
    void RemoveConnectionInloop(const PtrConnection & conn)
    {
        int id = conn->Id();
        auto it = _conns.find(id);
        if(it != _conns.end())
        {
            _conns.erase(it);
        }
    }
    void RemoveConnection(const PtrConnection & conn)
    {
        _baseloop.RunInloop(std::bind(&TcpServer::RemoveConnectionInloop , this, conn));
    }
    void AddTimerTaskInloop(const Functor& task , int timeout)
    {
        ++_id;
        _baseloop.TimerAdd(_id , timeout , std::move(task));
    }
public:
    TcpServer(int port):
        _id(0) , 
        _port(port) ,
        _timeout(0) ,
        _enable_intactive_release(false) , 
        _acceptor(&_baseloop , port),
        _pool(&_baseloop)
    {
        _acceptor.SetAcceptCallback([this](int fd){NewConnection(fd);});
        _acceptor.Listen();
    }
    void SetThreadNum(int num){ _pool.SetThreadNum(num) ;}
    void SetConnectedCallback(const ConnectedCallback&cb) { _connected_callback = cb; }
    void SetMessageCallback(const MessageCallback&cb) { _message_callback = cb; }
    void SetClosedCallback(const ClosedCallback&cb) { _closed_callback = cb; }
    void SetAnyEventCallback(const AnyEventCallback&cb) { _event_callback = cb; }
    void EnableInactiveRelease(int timeout) {_timeout = timeout ; _enable_intactive_release = true;}
    void AddTimerTask(const Functor& task , int timeout)
    {
        _baseloop.RunInloop([this , task = std::move(task) , timeout]()mutable{AddTimerTaskInloop(std::move(task), timeout);});
    }
    void Start()
    {
        _pool.Create();
        _baseloop.Start();
    }
};

class NetWork 
{
    public:
        NetWork() {
            DBG_LOG("SIGPIPE INIT");
            signal(SIGPIPE, SIG_IGN);
        }
};
static NetWork nw;