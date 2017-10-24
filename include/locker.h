//
// Created by woder on 10/18/17.
//

#ifndef LOCKER_H
#define LOCKER_H
#include <exception>
#include <pthread.h>
#include <semaphore.h>

//封装信号量的类
class sem{
public:
//    创建并且初始化信号量
    sem(){
         if( sem_init( &m_sem, 0, 0) != 0 ){
             throw std::exception();
         }

    }

//    销毁信号量
    ~sem(){
        sem_destroy( &m_sem );

    }

    //等待信号量
    bool wait(){
        return sem_wait( &m_sem ) == 0;

    }

    //增加信号量
    bool post(){
        return sem_post( &m_sem ) == 0;
    }

private:
    sem_t m_sem;

};

class locker{
public:
//    创建并初始化锁
    locker(){
        if( pthread_mutex_init( &m_mutex, NULL )  != 0 ){
            throw std::exception();
        }

    }
//    释放互斥锁
    ~locker(){
        pthread_mutex_destroy( &m_mutex ) == 0;

    }
    //锁
    bool lock(){
        return pthread_mutex_lock( &m_mutex ) == 0;

    }

    //释放
    bool unlock(){
        return pthread_mutex_unlock( &m_mutex ) == 0;
    }
private:
    pthread_mutex_t m_mutex;
};

class cond{
public:
    //产生条件变量
    cond(){
        //产生互斥量
        if( pthread_mutex_init( &m_mutex, NULL ) != 0 ){
            throw std::exception();
        }
        //产生条件变量
        if( pthread_cond_init( &m_cond, NULL ) != 0 ){
            pthread_mutex_destroy( &m_mutex );
            throw std::exception();
        }
    }

    ~cond(){
        //释放胡
        pthread_mutex_destroy( &m_mutex);
        pthread_cond_destroy( &m_cond );

    }


private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif //LOCKER_H
