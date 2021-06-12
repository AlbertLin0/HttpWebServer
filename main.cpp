#include "requestData.h"
#include "epoll.h"
#include "threadpool.h"
#include "util.h"

#include <sys/epoll.h>
#include <queue>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <unistd.h>

using namespace std;

const int THREADPOOL_THREAD_NUM = 4;
const int QUEUE_SIZE = 65535;

const int PORT = 8887;
// const int ASK_STATIC_FILE = 1;
// const int ASK_IMAGE_STITCH = 2;

const string PATH = "/";

const int TIMER_TIME_OUT = 500;


extern pthread_mutex_t qlock;
extern struct epoll_event* events;
void acceptConnection(int listen_fd, int epoll_fd, const string &path);

extern priority_queue<mytimer*, deque<mytimer*>, timerCmp> myTimerQueue;

int socket_bind_listen(int port)
{
    // 检查port值，取正确区间范围
    if (port < 1024 || port > 65535)
        return -1;

    // 创建socket(IPv4 + TCP)，返回监听描述符
    int listen_fd = 0;
    if((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return -1;

    // 消除bind时"Address already in use"错误
    int optval = 1;
    if(setsockopt(listen_fd, SOL_SOCKET,  SO_REUSEADDR, &optval, sizeof(optval)) == -1)
        return -1;

    // 设置服务器IP和Port，和监听描述副绑定
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short)port);
    if(bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        return -1;

    // 开始监听，最大等待队列长为LISTENQ
    if(listen(listen_fd, LISTENNUM) == -1)
        return -1;

    // 无效监听描述符
    if(listen_fd == -1)
    {
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

void myHandler(void *args)
{
    requestData *req_data = (requestData*)args;
    req_data->handleRequest();
}

void acceptConnection(int listen_fd, int epoll_fd, const string &path)
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    socklen_t client_addr_len = 0;
    int accept_fd = 0;
    while((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0)
    { 
        // 设为非阻塞模式
        int ret = setSocketNonBlocking(accept_fd);
        if (ret < 0)
        {
            perror("Set non block failed!");
            return;
        }

        requestData *req_info = new requestData(epoll_fd, accept_fd, path);

        // 文件描述符可以读，边缘触发(Edge Triggered)模式，保证一个socket连接在任一时刻只被一个线程处理
        __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
        epoll_add(epoll_fd, accept_fd, static_cast<void*>(req_info), _epo_event);
        // 新增时间信息
        mytimer *mtimer = new mytimer(req_info, TIMER_TIME_OUT);
        req_info->addTimer(mtimer);
        pthread_mutex_lock(&qlock);
        myTimerQueue.push(mtimer);
        pthread_mutex_unlock(&qlock);
    }
}
// 分发处理函数
void handle_events(int epoll_fd, int listen_fd, struct epoll_event* events, int events_num, const string &path, threadpool_t* tp)
{
    for(int i = 0; i < events_num; i++)
    {
        // 获取有事件产生的描述符
        requestData* request = (requestData*)(events[i].data.ptr);
        int fd = request->getFd();

        // 有事件发生的描述符为监听描述符
        if(fd == listen_fd)
        {
            acceptConnection(listen_fd, epoll_fd, path);
        }
        else
        {
            // 排除错误事件
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)
                || (!(events[i].events & EPOLLIN)))
            {
                printf("error event\n");
                delete request;
                continue;
            }

            // 将请求任务加入到线程池中
            // 加入线程池之前将Timer和request分离
            request->seperateTimer();
            int rc = threadpool_add(tp, myHandler, events[i].data.ptr, 0);
        }
    }
}


void handle_expired_event()
{
    pthread_mutex_lock(&qlock);
    while (!myTimerQueue.empty())
    {
        mytimer *ptimer_now = myTimerQueue.top();
        if (ptimer_now->isDeleted())
        {
            myTimerQueue.pop();
            delete ptimer_now;
        }
        else if (ptimer_now->isvalid() == false)
        {
            myTimerQueue.pop();
            delete ptimer_now;
        }
        else
        {
            break;
        }
    }
    pthread_mutex_unlock(&qlock);
}

int main()
{
    handle_for_sigpipe();
    int epoll_fd = epoll_init();
    if (epoll_fd < 0)
    {
        perror("epoll init failed");
        return 1;
    }
    threadpool_t *threadpool = threadpool_create(THREADPOOL_THREAD_NUM, QUEUE_SIZE, 0);
    int listen_fd = socket_bind_listen(PORT);
    if (listen_fd < 0) 
    {
        perror("socket bind failed");
        return 1;
    }
    if (setSocketNonBlocking(listen_fd) < 0)
    {
        perror("set socket non block failed");
        return 1;
    }
    __uint32_t event = EPOLLIN | EPOLLET;
    requestData *req = new requestData();
    req->setFd(listen_fd);
    epoll_add(epoll_fd, listen_fd, static_cast<void*>(req), event);
    while (true)
    {
        cout << "事件循环开始" << endl;
        int events_num = my_epoll_wait(epoll_fd, events, MAXEVENTS, -1);
        if (events_num == 0)
            continue;
        cout << "发生事件！事件数目：" << events_num << endl;
        handle_events(epoll_fd, listen_fd, events, events_num, PATH, threadpool);

        handle_expired_event();
    }
    return 0;
}