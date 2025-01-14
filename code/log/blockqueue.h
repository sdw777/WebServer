/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include <mutex>
#include <deque>
#include <condition_variable>
#include <sys/time.h>

template<class T>
class BlockDeque {
public:
    explicit BlockDeque(size_t MaxCapacity = 1000);

    ~BlockDeque();

    void clear();

    bool empty();

    bool full();

    void Close();

    size_t size();

    size_t capacity();

    T front();

    T back();

    void push_back(const T &item);

    void push_front(const T &item);

    bool pop(T &item);

    bool pop(T &item, int timeout);

    void flush();

private:
    std::deque<T> deq_;     // 队列容器，存放日志输入的信息

    size_t capacity_;       // 容器的容量

    std::mutex mtx_;        // 互斥锁

    bool isClose_;          // 关闭

    std::condition_variable condConsumer_;  // 生产者条件变量

    std::condition_variable condProducer_;  // 消费者条件变量
};


// 构造函数：设置容量
template<class T>
BlockDeque<T>::BlockDeque(size_t MaxCapacity) :capacity_(MaxCapacity) {
    assert(MaxCapacity > 0);
    isClose_ = false;
}

// 析构函数：关闭缓冲区
template<class T>
BlockDeque<T>::~BlockDeque() {
    Close();
};

// 关闭缓冲区
template<class T>
void BlockDeque<T>::Close() 
{
    // 利用lock_gurd的特点RALL
    {   
        std::lock_guard<std::mutex> locker(mtx_);
        deq_.clear();
        isClose_ = true;
    }
    condProducer_.notify_all();
    condConsumer_.notify_all();
};

// 刷新：读取缓冲区的数据（通知消费者）
template<class T>
void BlockDeque<T>::flush() {
    condConsumer_.notify_one();
};

// 清空缓冲区
template<class T>
void BlockDeque<T>::clear() {
    std::lock_guard<std::mutex> locker(mtx_);
    deq_.clear();
}

// 返回缓冲区头部元素：消费者
template<class T>
T BlockDeque<T>::front() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

// 返回缓冲区尾部元素：消费者
template<class T>
T BlockDeque<T>::back() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

// 获取缓冲区保存元素的数量
template<class T>
size_t BlockDeque<T>::size() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size();
}

// 获取缓冲区的容量
template<class T>
size_t BlockDeque<T>::capacity() {
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}

// 尾插法：生产者
template<class T>
void BlockDeque<T>::push_back(const T &item) {
    // 01：获取互斥锁
    std::unique_lock<std::mutex> locker(mtx_);
    // 02：判断是否有空间写入
    while(deq_.size() >= capacity_) {
        condProducer_.wait(locker);     // 没有空间进行写入，进行等待
    }
    deq_.push_back(item);
    condConsumer_.notify_one();         // 唤醒一个消费者
}

// 头插法：生产者
template<class T>
void BlockDeque<T>::push_front(const T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.size() >= capacity_) {
        condProducer_.wait(locker);
    }
    deq_.push_front(item);
    condConsumer_.notify_one();
}

// 判断是否为空
template<class T>
bool BlockDeque<T>::empty() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.empty();
}

// 判断是否已满
template<class T>
bool BlockDeque<T>::full(){
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size() >= capacity_;
}

// 获取元素
template<class T>
bool BlockDeque<T>::pop(T &item) {
    // 01：获取互斥锁
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.empty()){
        condConsumer_.wait(locker);         // 消费者进入阻塞

        if(isClose_){                       
            return false;
        }
    }
    item = deq_.front();                    // 读取头部元素
    deq_.pop_front();                       // 删除头部元素
    condProducer_.notify_one();             // 唤醒生产者
    return true;
}

// 获取元素
template<class T>
bool BlockDeque<T>::pop(T &item, int timeout) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.empty()){
        if(condConsumer_.wait_for(locker, std::chrono::seconds(timeout)) 
                == std::cv_status::timeout){
            return false;
        }
        if(isClose_){
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

#endif // BLOCKQUEUE_H