#ifndef HQW_THREADPOOL_HPP
#define HQW_THREADPOOL_HPP

#include <thread>
#include "SLink.hpp"

#define POOL_SIZE(x) (x>0) ? (x) : std::thread::hardware_concurrency():
namespace {

template <typename T>
class MailSlot {
   public:
      MailSlot();

      ~MailSlot();

      bool put(T&& t);
};

template <typename Queue, typename Slot>
class MailDispatcher {
   public:
      enum Status { STOPPED, RUNNING, STOPPING };

      MailDispatcher(Queue& q, Slot[] slots, size_t size)
         : m_queue(q), m_slots(slots), m_size(size), m_status(STOPPED)
      {}

      ~MailDispatcher();

      void stop();
      void start();
      
      Status getStatus() const
      {
         return m_status;
      }
   private:
      Queue& m_queue;
      Slot[] m_slots;
      size_t m_size;
      std::atomic<Status> m_status;
};

}

namespace hqw {

template <typename T>
class ThreadPool {
   public:
      explicit ThreadPool(size_t size = 0);
      ~ThreadPool();

      bool post(T&& t);

   private:
      static const size_t MAX_QUEUE_SIZE = 20;

      SLink<T> m_queue;
      std::unique_ptr<MailSlot<T>[]> m_slots;
      using Dispatcher = MailDispatcher<SLink<T>, MailSlot<T>>;
      Dispatcher m_dispatcher;
};


template <typename T>
ThreadPool<T>::ThreadPool(size_t size)
   : m_slots(std::unique_ptr<MailSlot<T>[]>(new MailSlot<T>[POOL_SIZE(size)])),
     m_dispatcher(m_queue, m_slots.get(), POOL_SIZE(size))
{
   m_dispatcher.start();
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
   m_dispatcher.stop();
}

template <typename T>
bool ThreadPool<T>::post(T&& t)
{
   if (m_dispatcher.getStatus() != Dispatcher::RUNNING ||
       m_queue.size() > MAX_QUEUE_SIZE) {
      return false;
   }

   m_queue.push(std::forward<T>(t));

   return true;
}


}
#endif
