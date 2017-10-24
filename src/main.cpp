#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <bits/sigaction.h>
#include <signal.h>

#include "locker.h"
#include "http_conn.h"
#include "threadpool.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;


extern int addfd( int epoollfd, int fd, bool one_shot );
extern int removefd( int epollfd, int fd);


void addsig( int sig, void ( handler ) (int), bool restart = true){

    //创建一个sigaction
    struct sigaction sa;
    //清空后设置handler
    memset( &sa, 0, sizeof( sa ));
    sa.sa_handler = handler;
    //添加额外的选项，重启由信号处理器所中断的系统调用
    if( restart ){
        sa.sa_flags |= SA_RESTART;
    }
    //将该信号处置运行时候的信号掩码设置成所有信号
    sigfillset(&sa.sa_mask);
    //设置sig的信号处置
    assert( sigaction( sig, &sa, NULL ) != -1);
}



int main( int argc, char* argv[] ){
    if( argc <= 2 ){
        printf("usage is filename ip_number port_number\n", basename( argv[0] ) );
        return 1;
    }

    //获取ip和端口
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    //将当前进程中的管道断开的信号处置设置成忽略
    addsig( SIGPIPE, SIG_IGN );

    //创建线程池
    threadpool < http_conn >* pool = NULL;
    try{
        pool = new threadpool< http_conn>;
    }
    catch(...){
        return 1;
    }

    //为每一个可能的客户连接分配一个http_conn对象
    http_conn* users = new http_conn[ MAX_FD ];
    assert( users );
    int user_count = 0;


    //设置监听端口的inet_v4地址
    struct sockaddr_in server_address;
    bzero( &server_address, sizeof( server_address ));
    server_address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &server_address.sin_addr );
    server_address.sin_port = htons( port );

    //获取socket
    int listenfd = socket( PF_INET, SOCK_STREAM, 0);
    assert( listenfd >= 0 );
    //设置该socket的属性，如果还有数据有待发送，则端口延迟关闭
    struct linger tmp={ 1, 0 };
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp) );

    //绑定socket和fd
    int ret = 0;
    ret = bind( listenfd, (struct sockaddr* )&server_address, sizeof( server_address));
    assert( ret >= 0 );

    //监听该fd
    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    //创建epoll对象，获取fd
    int epollfd = epoll_create( 5 );
    //创建epoll_event数组，用于接受返回的就绪队列
    epoll_event events[ MAX_EVENT_NUMBER ];
    //将监听端口放入到epoll对象中
    addfd( epollfd, listenfd , false);

    //开始epoll_wait并处理
    while( true  ){
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if( ( number < 0 ) && ( errno != EINTR ) ){//如果返回的数量小于0，或者阻塞被某个信号打断
            printf(" epoll failure\n");
            break;
        }

        for( int i=0 ; i< number ; i++){
            int sockfd = events[i].data.fd;
            if( sockfd == listenfd ){//监听端口有读/写/异常事件
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, (struct sockaddr*)& client_address, &client_addrlength);

                if( connfd < 0 ){
                    printf("errno is :%d\n", errno);
                    continue;
                }

                if( http_conn::m_user_count >= MAX_FD){
                    show_error( connfd, "Internal server busy");
                    continue;
                }

                //初始化客户连接
                users[ connfd ].init( connfd, client_address);
            }
            else if( events[i].events & (EPOLLRDHUP | EPOLLHUP |EPOLLERR )){
                //如果有异常,关闭客户端连接
                users[sockfd].close_conn();

            }
            else if( events[i].events & EPOLLIN ){
                //根据读的结果，决定是否将任务添加到线程池，还是关闭连接
                if( users[sockfd].read() ){
                    pool->append( users + sockfd );
                }
                else{
                    users[ sockfd ].close_conn();
                }
            }
            else if( events[i].events & EPOLLOUT ){

                if( !users[ sockfd ].write() ){
                    users[ sockfd ].close_conn();
                }
            }


        }

    }
    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}