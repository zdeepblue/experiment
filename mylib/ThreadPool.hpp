#ifndef HQW_THREADPOOL_HPP
#define HQW_THREADPOOL_HPP

#include <thread>
#include <condition_variable>

#include "SLink.hpp"

#define POOL_SIZE(x) (x>0) ? (x) : std::thread::hardware_concurrency():

namespace {

struct LockFreeLock
{
   void lock() {}

   void unlock() {}
};

using LockFreeCV = std::condition_variable_any;

template <typename T>
class MailSlot {
   public:
      MailSlot()
         : m_quit(false), m_hasMail(false), m_mail(),
           m_worker(std::bind(MailSlot::workerLoop, this))
      {
      }

      ~MailSlot()
      {
         m_quit = true;
         m_cvMail.notify_one();
         m_worker.join();
      }

      bool isEmpty() const
      {
         return !m_hasMail;
      }

      bool put(T&& t)
      {
         m_mail = std::forward<T>(t);
         m_hasMail = true;
         m_cvMail.notify_one();
      }
   private:
      void workerLoop();

      std::atomic<bool> m_quit;
      std::atomic<bool> m_hasMail;
      LockFreeCV m_cvMail;
      T m_mail;
      std::thread m_worker;
};

template <typename T>
MailSlot<T>::workerLoop()
{
   while (true) {
      m_cvMail.wait(m_lck, [this] () { return this->m_hasMail || this->m_quit;} );
      if (m_quit) {
         break;
      }
      try {
         m_mail();
      } catch (...) {
      }
   }
}

template <typename Queue, typename Slot>
class MailDispatcher {
   public:
      enum Status { STOPPED, RUNNING, STOPPING };

      MailDispatcher(Queue& q, Slot* slots, size_t size)
         : m_queue(q), m_slots(slots), m_size(size), m_status(STOPPED), m_quit(false)
           m_disp(std::bind(&MailDispatcher::dispatchLoop, this))
      {
      }

      ~MailDispatcher()
      {
         stop();
         m_quit = true;
         m_disp.join();
      }

      void stop()
      {
         if (m_status != RUNNING) return;
         m_status = STOPPING;
         m_cvQueue.notify_one();
      }

      void start()
      {
         if (m_status != STOPPED) return;
         m_status = RUNNING;
         m_cvQueue.notify_one();
      }
      
      Status getStatus() const
      {
         return m_status;
      }

      void gotMail()
      {
         m_cvQueue.notify_one();
      }


   private:
      void dispatchLoop();

      Queue& m_queue;
      Slot* m_slots;
      size_t m_size;
      std::atomic<Status> m_status;
      std::atomic<bool> m_quit;
      LockFreeLock m_lck;
      LockFreeCV m_cvQueue;
      LockFreeCV m_cvStatus;
      std::thread m_disp;
};

template <typename Queue, typename Slot>
MailDispatcher<Queue, Slot>::dispatchLoop()
{
   while (true) {
      m_cvStatus.wait(m_lck, [this] () {return this->m_status != STOPPED || m_quit; } );
      if (m_quit) {
         break;
      }

      m_cvQueue.wait(m_lck, [this] () {return !this->m_queue.empty() || this->m_status==STOPPING;} );
      auto t = m_queue.pop();
      if (!t.hasValue()) {
         if (m_status == STOPPING) {
            m_status = STOPPED;
         }
         continue;
      }
      size_t i = 0;
      for (; i < m_size ; ++i) {
         if (m_slots[i].isEmpty()) {
            typename Queue::value_type tmp(t);
            m_slots[i].put(std::move(tmp));
            break;
         }
      }
      if (i == m_size) {
         m_queue.pushRef(std::move(t));
      }
   }
}

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
   m_dispatcher.gotMail();

   return true;
}


}
#endif
