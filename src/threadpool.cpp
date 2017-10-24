//
// Created by woder on 10/18/17.
//

#include "threadpool.h"

template< typename T >
threadpool< T > :: threadpool( int thread_number, int max_requests ): m_thread_number( thread_number), m_max_requests( max_requests ),
                                                                      mstop( false ), m_thread( NULL ){
    if( ( thread_number <= 0 ) || ( max_requests <= 0 ) ){
        throw std::exception();
    }

    //创建线程句柄数组
    m_threads = new pthread_t[ m_threada_number ];
    if( !m_threads ){//如果为空的话
        throw std::exception();
    }

    //创建线程，并且设置成脱离线程，结束后无需wait，将会回收资源
    for( int i=0; i < thread_number; i++ ){
        printf(" create the %dth thread\n ", i );
        if( pthread_create( m_threads + i, NULL, worker, this  ) != 0){
            delete [] m_threads;
            throw std::exception();
        }
        if( pthread_detach( m_threads[i]  )){
                delete[]m_threads;
            throw std::exception();
        }

    }
}

//释放线程句柄数组，并且终止各个线程
template < typename T >
threadpool< T >::~threadpool() {
        delete [] m_threads;
        m_stop = true;
}

//添加工作
template < typename T>
bool threadpool< T >:: append( T* request){
    //对工作队列上锁
    m_queuelocker.lock();
    if( m_workqueue.size() > m_max_requests ){
        m_queuelocker.unlock();
        return false;
    }
    //将请求放入队列中
    m_workqueue.push_back( request );
    m_queuelocker.unlock();
    //对信号量进行+1操作
    m_queuestat.post();

}

template < typename T>
threadpool < T >::worker( void *arg ){
    threadpool* pool = (threadpool* ) arg;
    pool->run();
    return pool;
}

template< typename T >
threadpool < T >:: run(){
    while( !stop ){

        m_queuestat.wait();//信号量的数值就是当前可以处理的任务数，这里使用信号量是为了同步，
        m_queuelocker.lock();//操作工作队列，枷锁
        if( m_workqueue.empty() ){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();//对工作队列进行解锁
        if( !request ){
            continue ;
        }
        request->process();
    }
}