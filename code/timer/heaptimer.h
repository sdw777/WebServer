/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 
#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h> 
#include <functional> 
#include <assert.h> 
#include <chrono>
#include "../log/log.h"

typedef std::function<void()> TimeoutCallBack;      // 回调函数
typedef std::chrono::high_resolution_clock Clock;    
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;                // 时间节点：17：06：24

struct TimerNode {
    int id;             // 文件描述符
    TimeStamp expires;  // 超时时间
    TimeoutCallBack cb; // 回调函数
    bool operator<(const TimerNode& t) {
        return expires < t.expires;
    }
};



class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }
    ~HeapTimer() { clear(); }
    
 
    void adjust(int id, int newExpires);

    void add(int id, int timeOut, const TimeoutCallBack& cb);

    void doWork(int id);

    void clear();

    void tick();

    void pop();

    int GetNextTick();

private:
    void del_(size_t i);
    
    void siftup_(size_t i);
    
    bool siftdown_(size_t index, size_t n);
   
    void SwapNode_(size_t i, size_t j);

    std::vector<TimerNode> heap_;               // 存放节点

    std::unordered_map<int, size_t> ref_;       // 节点和下标的映射关系
};

#endif //HEAP_TIMER_H