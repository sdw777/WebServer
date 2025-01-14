/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */ 
#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

// 获取可读字节数
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

// 获取可写字节数
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

// 获取已读字节数
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

// 读指针
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

// 读取数据后，读指针后移
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
}

// 将读指针移到最后位置
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end );
    Retrieve(end - Peek());
}

// 缓冲区清零
void Buffer::RetrieveAll() {
    bzero(&buffer_[0], buffer_.size());     
    readPos_ = 0;
    writePos_ = 0;
}

// 读取剩余的所有
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());   // 将剩余未读的数据读入str字符串中
    RetrieveAll();
    return str;
}

// 写指针
const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

// 写指针
char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

// 写数据后，写指针后移
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
} 

// 将字符串写入缓冲区
void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}

// 将一块内存中的数据写入缓冲区
void Buffer::Append(const void* data, size_t len) {
    assert(data);
    Append(static_cast<const char*>(data), len);
}

// 将char*类型的数据写入缓冲区（核心插入函数）
void Buffer::Append(const char* str, size_t len) {
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}


void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

// 确保写入的字节数小于可写入的字节数
void Buffer::EnsureWriteable(size_t len) {
    if(WritableBytes() < len) {
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}


/*
struct iovec {
    void *iov_base; // 缓冲区起始地址
    size_t iov_len; // 缓冲区长度
};

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
其中：
fd：文件描述符
iov:iovec结构体数组
iovcnt:数组元素个数。
*/


// 从文件中读数据写入缓冲区
ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    // 临时缓冲区，保证能把所有的数据都读出来
    char buff[65535];
    struct iovec iov[2];
    const size_t writable = WritableBytes();    // 当前缓冲区可写入数据的大小

    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = BeginPtr_() + writePos_;
    iov[0].iov_len = writable;

    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);

    if(len < 0) 
    {
        *saveErrno = errno;
    }
    else if(static_cast<size_t>(len) <= writable)       // 可写的字节数>=读入字节数
    {
        writePos_ += len;
    }
    else 
    {
        writePos_ = buffer_.size();
        Append(buff, len - writable);
    }
    return len;
}

// 将缓冲区的数据写入文件
ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if(len < 0) 
    {
        *saveErrno = errno;
        return len;
    } 
    readPos_ += len;
    return len;
}

// 获取缓存的首地址
char* Buffer::BeginPtr_() {
    return &*buffer_.begin();
}

// 获取缓存的首地址
const char* Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}


// 更新缓存大小
void Buffer::MakeSpace_(size_t len) {
    // 01:可写字节数+已读字节数<写入字节数（扩容）
    if(WritableBytes() + PrependableBytes() < len) 
    {
        buffer_.resize(writePos_ + len + 1);
    }
    // 02:可写字节数+已读字节数>=写入字节数（移动，类似于循环数组）
    else 
    {
        
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}