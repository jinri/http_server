#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include<cstdio>
#include<exception>
#include<pthread.h>

#include "locker.h"

template<typename T>
class threadpool
{
private:
     int 			m_thread_number;
     int 			m_max_requests;
     pthread_t* 	m_threads;      //线程组
     std::list<T*> 	m_workqueue;
     locker 		m_queuelocker;
     sem 			m_queuestat;
     bool 			m_stop;

private:
      static void* worker(void* arg);
      void 	 run();

public:
       threadpool(int thread_number = 8,int max_requests = 10000);
       ~threadpool();
       bool append(T* request);
};

template<typename T>
threadpool<T>::threadpool(int thread_number,int max_requests):
             m_thread_number(thread_number),m_max_requests(max_requests),m_stop(false),m_threads(NULL)
{
       if((thread_number <= 0) || (max_requests <= 0))
       {
             throw std::exception();
       }
       m_threads = new pthread_t[m_thread_number];
       if (!m_threads)
       {
             throw std::exception();
       }
      
       for (int i = 0;i < thread_number;++i)
       {
             printf("create the %dth thread\n",i);
             if(pthread_create(m_threads+i,NULL,worker,this) != 0) //this 将类的对象作为参数传递给静态函数，就可以在静态函数中
             {                                                     // 访问类的动态成员
                delete [] m_threads;
                throw  std::exception();
             }
             if(pthread_detach(m_threads[i]))                      //将线程设置为脱离线程，其推出时自行释放所占资源
             {
                delete [] m_threads;
                throw  std::exception();
             }
       }
}
 
template<typename T>
threadpool<T>::~threadpool()
{
       delete [] m_threads;
       m_stop = true;
}
template<typename T>
bool threadpool<T>::append(T* request)
{
     m_queuelocker.lock();                 //请求队列加锁
     if(m_workqueue.size() > m_max_requests)
     {
       m_queuelocker.unlock();
       return false;
     }
     m_workqueue.push_back(request);        // 将一个元素追加到一个容器的后面　
     m_queuelocker.unlock();                // 解锁
     m_queuestat.post();                    // 将信号量的值＋１，当信号量的值大于０时，其他正在调用sem_wait的线程会被唤醒
     return  true;                          
}
  
template<typename T>
void* threadpool<T>::worker(void* arg)
{
     threadpool* pool = (threadpool*)arg;
     pool->run();
     return pool;
}

template<typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
       m_queuestat.wait();
       m_queuelocker.lock();
       if (m_workqueue.empty())
       {
           m_queuelocker.unlock();
           continue;
       }
       T* request = m_workqueue.front();
       m_workqueue.pop_front();
       m_queuelocker.unlock();
       if(!request)
       { 
          continue;
       }
       request->process();
    }
}
#endif
