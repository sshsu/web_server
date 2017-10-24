//
// Created by woder on 10/19/17.
//
#include "http_conn.h"

const char* ok_200_title = "OK";
const char* error_400_title = "Bad_Request";
const char* error_400_form = "Your request has bad syntax or is herently impossiblr to satisfy.\n";
const char* error_403_title =  "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = " There was an unusual problem serving the requested file.\n";

//网站的根目录
const char* doc_root="/var/www/html";


int setnonblocking( int fd){
    //设置该描述为非阻塞必用且返回旧的属性用于恢复
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}



void addfd( int epollfd, int fd, bool one_shot){
    //将文件描述符以及对应的事件生成epoll_event 放入到epoll对象中
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if( one_shot ){//最多触发其上发生的一个/可读/可写/可触发事件并且只能触发一次，防止去多个线程处理一个socket
        event.events |= EPOLLONESHOT;
    }
    //将该event加入到当前的epoll对象中
    epoll_ctl( epollfd, EPOLL_CTL_ADD,  fd, &event );
    setnonblocking( fd );

}

//删除fd事件
void removefd( int epollfd, int fd){
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );

}

//
void modfd( int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd ;
    event.events = (uint32_t)(ev) | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );

}


void http_conn::init( int sockfd, const sockaddr_in& addr){
    m_sockfd = sockfd;
    m_address = adr;
    //为了避免time_wait状态
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ));
    addfd( m_epollfd, sockfd, true );
    m_user_count++;
    init();

}

void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;


    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_ id = 0;
    memset( m_read_bud, 0, READ_BUFFER_SIZE);
    memset( m_write_buf, 0, WRITE_BUFFER_SIZE);
    memset( m_real_file, 0, FILENAME_LEN);

}
//读入函数
bool http_conn::read(){
    if( m_read_idx >= READ_BUFFER_SIZE ){
        return false;
    }

    int bytes_read = 0;
    while( true ){
        //从sock中读入数据
        bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
        if( bytes_read == -1 ){
            if( errno  == EAGAIN || errno == EWOULDBLOCK ){
                break;
            }
            return false;
        }
        else if( bytes_read == 0 ){
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}




//http的报文格式为：请求行+ 请求头+ 请求正文，逐一进行解析
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    //解析出一个完整的行
    while( ( m_check_state == CHECK_STATE_CONTENT ) && ( line_status == LINE_OK )
            || ( ( line_status == parse_line()) == LINE_OK ) ){
        text = get_line();//获取当前行的
        m_start_line = m_checked_idx;//初始化下一行的位置
        printf("got 1 http line：  %s\n", text );

        switch( m_check_state ){
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_request_line( text );//解析请求行
                if( ret == BAD_REQUEST ){
                    return BAD_REQUEST;

                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_headers( text );    //解析请求头
                if( ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if( ret == GET_REQUEST ){
                    return do_request();
                }

            }


        }
    }

}

//解析出完整的行，由于每行都是由\r\n结束的
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for( ; m_checked_idx < m_read_idx ; ++m_checked_idx  ){
        temp = m_read_buf[ m_checked_idx ];
        if( temp == '\r' ){//第一个是'\r'
            if( ( m_checked_idx + 1  ) == m_read_idx){
                return LINE_OPEN;
            }
            else if( m_read_buf[ m_checked_idx + 1 ] == '\n' ){
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
            }
        }
        else if( temp == '\n' ){//当前读入的是\n，判断前一个是否是\r
            if( (m_checked_idx > 1 ) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' )){
                m_read_buf[ m_checked_idx - 1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//处理请求结构，主要是处理请求方法，请求http协议和版本以及处理请求url
http_conn::HTTP_CODE http_conn::parse_request_line( char* text ){

    //http请求结构： [请求方法]+' '+[请求url]+' '+[http协议及版本]+[回车]+[换行]
    //找到第一个空格或者\t
    m_url = strpbrk( text, " \t");
    //如果没有空格或者\t，请求结构有错
    if( !m_url){
        return BAD_REQUEST;
    }
    //将第一个空格置成\0
    *m_url++ = '\0';

    //这里只处理文件头为GET的情况，如果不是则 不处理
    char* method = text;
    if( strcasecmp( method, "GET") == 0 ){
        m_method = GET;
    }
    else{
        return BAD_REQUEST;
    }


    //处理协议以及版本
    m_url += strspn( m_url, " \t" );      //找到第一个非空格+\t字符，就是请求url的第一个位置,总是感觉这一步很多余
    m_version = strpbrk( m_url, " \t" );  //再找到url前面的空格

    if( !m_version ){
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn( m_version, " \t" );//重新定位

    if( strcasecmp( m_version, "HTTP/1.1") != 0 ){
        return BAD_REQUEST;
    }


    //处理url
    if( strncasecmp( m_url, "http://", 7) == 0 ){
        m_url += 7;
        m_url = strchr( m_url, '/' );//找到第一个'/'
    }
    //找到第二个 ‘/’（一共就两级目录）
    if( !m_url || m_url[0] != '/' ){
        return BAD_REQUEST;
    }
    m_check_state - CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析请求头 格式：
//[头部字段名]:[值][回车][换行符]
//[头部字段名]:[值][回车][换行符]
//...
//[回车][换行符]
http_conn::HTTP_CODE http_conn::parse_headers( char* text){

    if( test[0] == '\0' ){//遇到空行，头部字段解析完毕
        if( m_content_length != 0 ){//内容部分不为0
            m_check_state = CHECK_STATE_CONTENT;//接下来检查正文内容
            return NO_REQUEST;
        }
        return GET_REQUEST;

    }
    //处理connection头部字段
    else if( strncasecmp( text, "Connection:", 11) == 0 ){
        text += 11;
        text += strcpn( text, " \t");//重新定位，跳过后面的空格
        if( strcasecmp( text, "keep-alive") == 0 ){
            m_linger = true;
        }
    }
    //处理content-length头部字段
     else if( strncasecmp( text, "Content-Length:", 15) == 0 ){
        text += 15;
        text += strspn( text,  " \t");
        m_content_length = atol( text );

    }
    //处理host头部字段
    else if( strncasecmp( text, "Host:", 5) == 0 ){
        text += 5;
        text += strspn( text, " \t");
        m_host = text;
    }
    else{
        printf("oop! unknow header %s\n", text);
    }

    return NO_REQUEST;
};
//当得到一个完整的，正确的HTTP请求时，就分析目标文件的属性。如果目标文件存在，对所有用户可读并且不是目录，则使用mmap将
//其映射到内存地址m_file_address出，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy( m_real_file, doc_root );//先将文件的根目录路径拷贝
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN -len -1 );//将请求的文件放入
    if( stat( m_real_file, &m_file_stat ) < 0){ //如果文件不存在
        return NO_RESOURCE;
    }
    if( !(m_file_stat.st.mode & S_IROTH )){
        return FORBIDDEN_REQUEST;
    }

    if( S_ISDIR( m_file_stat.st_mode )){//是否是一个目录
        return BAD_REQUEST;
    }

    //以只读形式打开
    int fd = open( m_real_file, O_RDONLY );
    //映射到内存
    m_file_address = (char*)mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close( fd );
    return FILE_REQUEST;
}

void http_conn::unmap(){

    if( m_file_address ){
        munmap( m_file_address, m_file_stat.st_size);

    }

}


//写http响应
bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if( bytes_to_send == 0 ){
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        init();
        return true;
    }

    while( 1 ){
        temp = writev( m_sockfd, m_iv, m_iv_count );
        if( temp <= -1 ){
            //非阻塞操作不成功，即tcp写缓存没有空间，需要等待下一轮的EPOLLOUT事件。
            // 虽然在此期间，服务器无法立即接收到同一客户的下一个请求，但是可以保持连接的完整性
            if( errno == EAGAIN ){
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            //释放映射的内存空间
            unmap();
            return true;
        }


        bytes_to_send -= temp;
        bytes_have_send += temp;
        if( bytes_to_send <= bytes_have_send ){//这个判断是不是有bug?
            unmap();
            if( m_linger ){
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return ture;
            }
            else{
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            }
        }
    }


}


void http_conn::process(){
    HTTP_CODE read_ret = process_read();
    if( read_ret == NO_REQUEST ){//还有收到一个完整的请求，继续读数据
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }

    bool write_ret = process_write( read_ret );//如果没有把内容写进写缓存中
    if( !write_ret ){//可能是由于资源等原因请求失败，关闭连接
        close_conn();
    }

    //内容写入缓存中，更新epoll对象中sockfd的事件
    modfd( m_epollfd, m_sockfd, EPOLLOUT );

}