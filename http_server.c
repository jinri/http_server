#include<sys/socket.h> //C library
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<sys/epoll.h>

#include<cassert> //C++ libary

#include"locker.h"
#include"threadpool.h"
#include"http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd,int fd,bool one_shot);
extern int removefd(int epollfd,int fd);

void addsig(int sig,void(handler)(int) )
{
   bool restart = true;
   struct sigaction sa;
   memset(&sa,'\0',sizeof(sa));
   sa.sa_handler = handler;
   if(restart)
   {
      sa.sa_flags |= SA_RESTART;
   }
   sigfillset( &sa.sa_mask);
   assert(sigaction(sig,&sa,NULL) != -1);
}

void show_error(int connfd,const char* information)
{
   printf("%s", information);
   send(connfd,information,strlen(information),0);
   close(connfd);
}

int main(int argc, char* argv[])
{
   if(argc <= 2)
   {  
       printf("usage: %s ip_address port_number\n",basename(argv[0]));
       return 1;
   }
   const  char* ip = argv[1];
   int port = atoi(argv[2]);

   addsig(SIGPIPE,SIG_IGN);//SIGPIPE 信号：往读端被关闭的管道或者socket连接中写数据
                           //SIG_IGN :信号的处理方式，表示忽略目标信号
   threadpool<http_conn>* pool = NULL;
   try
   {
       pool = new threadpool<http_conn>; //模板在这的作用？
   }
   catch(...)                   //能捕获多种数据类型的异常对象
   {
      return 1;
   }

   http_conn* users = new  http_conn[MAX_FD];
   assert(users);
   int user_count = 0;

   int  listenfd = socket(PF_INET,SOCK_STREAM,0);
   assert(listenfd >= 0);
   struct  linger tmp = {1,0};
   setsockopt(listenfd, SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));// SOL_SOCKET 通用socket选项
                                                               // SO_LINGER 若有数据发送，则延迟关闭

   int  ret=0;
   struct sockaddr_in address;
   bzero(&address, sizeof(address));
   address.sin_family = AF_INET;
   inet_pton(AF_INET,ip,&address.sin_addr);
   address.sin_port = htons(port);                             //主机类型转化为网络类型

   ret =bind( listenfd, (struct sockaddr*)&address,sizeof(address));
   assert(ret >= 0);

   ret = listen(listenfd,5);
   assert(ret >= 0);

   epoll_event events[MAX_EVENT_NUMBER];                      //　epoll内核事件表
   int epollfd = epoll_create(5);                             // 创建一个文件描述符，标识这个内核事件表
   assert(epollfd != -1);
   addfd(epollfd,listenfd,false);
   http_conn::m_epollfd = epollfd;

   while(true)
   {
          int  number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER, -1);
          if ((number < 0)&&(errno != EINTR))
          {
               printf("epoll failure\n");
               break;
          }
          for (int i = 0;i < number;i++)
          {
               int sockfd = events[i].data.fd;
               if( sockfd == listenfd)
               {
                    struct  sockaddr_in  client_address;
                    socklen_t client_addrlength = sizeof(client_address);
                    int connfd = accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
                    if(connfd < 0)
                    {
                        printf("errno is: %d\n",errno);
                        continue;
                    }
                    if(http_conn::m_user_count >= MAX_FD)
                    {
                        show_error(connfd,"Internal server busy");
                        continue;
                    }
                    users[connfd].init(connfd,client_address);
               }
               else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
               {
                    users[sockfd].close_conn();
               }
               else if (events[i].events & EPOLLIN)
               {
                    if(users[sockfd].read())
                    {
                         pool->append(users + sockfd);
                    }
                    else
                    {
                         users[sockfd].close_conn();
                    }
               }
               else if(events[i].events & EPOLLOUT)
               {
                    if(!users[sockfd].write())
                    {
                         users[sockfd].close_conn();
                    }
               }
               else
               {}
           }
       }
      
       close(epollfd);
       close(listenfd);
       delete[] users;
       delete pool;
       return 0;
}  
