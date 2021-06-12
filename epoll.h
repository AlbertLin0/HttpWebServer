#ifndef EPOLL_H
#define EPOLL_H
#include "requestData.h"

const int MAXEVENTS = 1024;
const int LISTENNUM = 1024;   /* 大致的内核监听数目 新的linux版本已经移除epoll_create参数*/

int epoll_init();
/* 包裹函数 */
int epoll_add(int epoll_fd, int fd, void *request, __uint32_t events);
int epoll_mod(int epoll_fd, int fd, void *request, __uint32_t events);
int epoll_del(int epoll_fd, int fd, void *request, __uint32_t events);
int my_epoll_wait(int epoll_fd, struct epoll_event *events, int max_events, int timeout);

#endif