//
//  ThreadSafeQueueTemp.hpp
//  video_player
//
//  Created by wbxie on 2024/2/22.
//

#ifndef ThreadSafeQueueTemp_hpp
#define ThreadSafeQueueTemp_hpp

#include <stdio.h>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <memory>

template <class T>
class ThreadSafeQueueTemp
{
public:
    ThreadSafeQueueTemp(){}
    ThreadSafeQueueTemp(ThreadSafeQueueTemp const& other)
    {
        std::lock_guard<std::mutex> lk(other._mutex);
        _queue = other._queue;
    }

    void push(T newData)
    {
        std::lock_guard<std::mutex> lk(_mutex);
//        printf("push.");
        _queue.push(newData);
        _dataCond.notify_one();

    }

    T waitAndPop()
    {
        std::unique_lock<std::mutex> lk(_mutex);
        _dataCond.wait(lk,[this]{
            return !_queue.empty();
        });
        T t = _queue.front();
        _queue.pop();
        return t;

    }

    T tryPop()
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if(_queue.empty())
        {
            return T();
        }
        T t =  _queue.front();
        _queue.pop();
        return t;
    }
    
    unsigned long size(){
        std::lock_guard<std::mutex> lk(_mutex);
        return _queue.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(_mutex);
        while(!_queue.empty())
        {
            _queue.pop();
        }

    }

public:
    std::queue<T> _queue;

private:

    std::condition_variable _dataCond;
    std::mutex _mutex;
};


#endif /* ThreadSafeQueueTemp_hpp */
