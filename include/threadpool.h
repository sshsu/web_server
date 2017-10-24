//
// Created by woder on 10/18/17.
//

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include"locker.h"

template< typename T>
class threadpool{
private:
    int m_thread_number; //线程池中的线程数
    int m_max_requests; //请求队列中允许的最大请求数
    pthread_t* m_threads;//描述线程池的数组，其大小为m_thread_number
    std::liust< T* > m_workqueue; //请求队列
    locker m_queuelocker;//请求队列互斥锁
    sem m_queuestat; //是否有任务需要处理
    bool m_stop;//是否结束线程
public:
    threadpool( int thread_number = 8, int max_requests = 10000 );
    ~threadpool();
    //向请求队列中添加内容
    bool append( T* request);

private:
    static boid* worker( void* arg);
    void run();

};

#endif //THREADPOOL_H
