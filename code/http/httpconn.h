/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>      

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn {
public:
    // 构造函数
    HttpConn();
    // 虚构函数
    ~HttpConn();
    // 初始化
    void init(int sockFd, const sockaddr_in& addr);
    // 读取
    ssize_t read(int* saveErrno);
    // 写入
    ssize_t write(int* saveErrno);
    // 关闭
    void Close();
    // 获取与客户端进行通信的套接字
    int GetFd() const;
    // 获取通信端口
    int GetPort() const;
    // 获取客户端IP
    const char* GetIP() const;
    // 获取客户端地址信息
    sockaddr_in GetAddr() const;
    
    bool process();

    int ToWriteBytes() { 
        return iov_[0].iov_len + iov_[1].iov_len; 
    }

    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }

    static bool isET;
    static const char* srcDir;              // 资源目录
    static std::atomic<int> userCount;      // 总共客户端的连接数
    
private:
   
    int fd_;                                // 服务端套接字
    struct  sockaddr_in addr_;              // 客户端地址信息

    bool isClose_;                          // 是否关闭连接
    
    int iovCnt_;                            
    struct iovec iov_[2];                   // 缓冲区，用于分散读和聚集写
    
    Buffer readBuff_;                       // 读缓冲区，保存请求数据的内容
    Buffer writeBuff_;                      // 写缓冲区，保存响应数据的内容

    HttpRequest request_;                   // 接收报文
    HttpResponse response_;                 // 响应报文
};


#endif //HTTP_CONN_H