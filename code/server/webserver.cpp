/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */

#include "webserver.h"

using namespace std;
#define SERVERIP "192.168.248.133"

// 初始化
WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
        // 获取当前路径（/ home/sanxian/C++/WebServer-master/resources）
        srcDir_ = getcwd(nullptr, 256); 
        assert(srcDir_);
        // 拼接成资源路径
        strncat(srcDir_, "/resources/", 16);

        // 初始化HTTP连接信息
        HttpConn::userCount = 0;
        HttpConn::srcDir = srcDir_;

        // 初始化数据库连接池
        SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

        // 初始化事件模式
        InitEventMode_(trigMode);

        // 初始化服务端套接字
        if (!InitSocket_())
        {
            isClose_ = true;
        }

        
    // 日志设置
    if(openLog) 
    {

        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            //std::cout << "初始化成功！" << endl;
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

// 析构
WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

// 设置监听的文件描述符和通信的文件描述符的模式
void WebServer::InitEventMode_(int trigMode) {
    /*
    EPOLLIN：表示对应的文件描述符可以读；
    EPOLLOUT：表示对应的文件描述符可以写；
    EPOLLPRI：表示对应的文件描述符有紧急的数据可读；
    EPOLLERR：表示对应的文件描述符发生错误；
    EPOLLHUP：表示对应的文件描述符被挂断；
    EPOLLET：表示对应的文件描述符有事件发生；
    EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个
                  socket加入到EPOLL队列里
    */
    listenEvent_ = EPOLLRDHUP;                  
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;      
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }

    HttpConn::isET = (connEvent_ & EPOLLET);
}


void WebServer::Start() {
    int timeMS = -1;            /* epoll wait timeout == -1 无事件将阻塞 */

    // 01：检测服务端是否关闭
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    
    while(!isClose_) {
        //std::cout << "running!!!" << endl;
        if (timeoutMS_ > 0)
        {
            timeMS = timer_->GetNextTick();
        }

        int eventCnt = epoller_->Wait(timeMS);     //阻塞
        for(int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);

            if(fd == listenFd_) 
            {
                // 处理新连接
                DealListen_();       
            }
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) 
            {
                // 连接出现错误（关闭连接）
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            }
            else if(events & EPOLLIN) 
            {
                // 文件描述符读数据（读操作）
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            else if(events & EPOLLOUT) 
            {
                // 文件描述符写数据（写操作）
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } 
            else 
            {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}


// 发送错误信息
void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

// 关闭连接
void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    // 01：取消监测
    epoller_->DelFd(client->GetFd());
    // 02：关闭客户端连接
    client->Close();
}

// 添加连接
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    // 01：保存客户端信息
    users_[fd].init(fd, addr);      
    if(timeoutMS_ > 0) 
    {
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    // 02：进行监测
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    // 03：设置为非阻塞
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

// 处理监听套接字所接收的信息
void WebServer::DealListen_() {
    struct sockaddr_in addr;            // 保存连接的客户端的地址信息
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0)  // 连接失败
        { 
            return;
        }
        else if(HttpConn::userCount >= MAX_FD)  // 客户端连接数量大于最大连接数量
        {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd, addr);
    } while(listenEvent_ & EPOLLET);
}

// 处理其他套接字的读操作
void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

// 处理其他套接字的写操作
void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

// 时间调整
void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

// 子线程中执行
void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);         // 读取客户端数据

    if(ret <= 0 && readErrno != EAGAIN)     // 出现错误直接关闭
    {
        CloseConn_(client);
        return;
    }

    // 处理，进行业务逻辑的处理
    OnProcess(client);
}

void WebServer::OnProcess(HttpConn* client) {
   
    if(client->process()) 
    {
        // 读完后，将相应的文件描述符改为写状态
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } 
    else
    {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

/* Create listenFd */
bool WebServer::InitSocket_() {

    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, SERVERIP, &addr.sin_addr.s_addr);
    //addr.sin_addr.s_addr = A
    addr.sin_port = htons(port_);


    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    // 01：创建监听套接字
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);

    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    // 02：绑定地址和端口
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 03:监听
    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 将监听文件描述符加入epoll中
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);       // 设置监听文件描述符非阻塞
    LOG_INFO("Server port:%d", port_);

    return true;
}

// 设置文件描述符非阻塞
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);

    // int flag = fcntl(fd, F_GETFD, 0);   // 获取原先的值
    // flag = flag | O_NONBLOCK;
   
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


