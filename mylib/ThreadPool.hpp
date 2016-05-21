#ifndef HQW_THREADPOOL_HPP
#define HQW_THREADPOOL_HPP

#include <thread>
#include <atomic>
#include <condition_variable>

#include "SLink.hpp"

#define POOL_SIZE(x) (x>0) ? (x) : std::thread::hardware_concurrency()

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
         : m_quit(false), m_pMail(nullptr),
           m_worker(std::bind(&MailSlot::workerLoop, this))
      {
      }

      ~MailSlot()
      {
         m_quit = true;
         m_cvMail.notify_one();
         m_worker.join();
      }

      bool put(T t)
      {
         if (m_pMail != nullptr) return false;
         m_mail = std::move(t);
         m_pMail = &m_mail;
         m_cvMail.notify_one();
         return true;
      }
   private:
      void workerLoop();

      std::atomic<bool> m_quit;
      std::atomic<T*> m_pMail;
      LockFreeLock m_lck;
      LockFreeCV m_cvMail;
      T m_mail;
      std::thread m_worker;
};

template <typename T>
void MailSlot<T>::workerLoop()
{
   while (true) {
      m_cvMail.wait(m_lck, [this] () { return this->m_pMail != nullptr || this->m_quit;} );
      if (m_quit && m_pMail == nullptr) {
         break;
      }
      try {
         m_mail();
      } catch (...) {
      }
      m_pMail = nullptr;
   }
}

template <typename Queue, typename Slot>
class MailDispatcher {
   public:
      enum Status { STOPPED, RUNNING, STOPPING };

      MailDispatcher(Queue& q, Slot* slots, size_t size)
         : m_queue(q), m_slots(slots), m_size(size), m_status(STOPPED), m_quit(false),
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
         m_cvStatus.notify_one();
      }

      void start()
      {
         if (m_status != STOPPED) return;
         m_status = RUNNING;
         m_cvStatus.notify_one();
         m_cvQueue.notify_one();
      }
      
      void gotMail()
      {
         m_cvQueue.notify_one();
      }


   private:
      void dispatchLoop();

      Queue& m_queue;
      Slot* const m_slots;
      const size_t m_size;
      std::atomic<Status> m_status;
      std::atomic<bool> m_quit;
      LockFreeLock m_lck;
      LockFreeCV m_cvQueue;
      LockFreeCV m_cvStatus;
      std::thread m_disp;
};

template <typename Queue, typename Slot>
void MailDispatcher<Queue, Slot>::dispatchLoop()
{
   while (true) {
      m_cvStatus.wait(m_lck, [this] () {return this->m_status != STOPPED || this->m_quit; } );
      if (m_quit && m_status==STOPPED) {
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
         if (m_slots[i].put(*t)) {
            break;
         }
      }
      if (i == m_size) {
         m_queue.push(std::move(t));
      }
   }
}

}

namespace hqw {

template <typename T>
class ThreadPool {
      static const size_t DEFAULT_QUEUE_SIZE = 20;
   public:
      ThreadPool(size_t queue_size = DEFAULT_QUEUE_SIZE, size_t pool_size = 0);
      ~ThreadPool();

      bool post(T t);

   private:
      const size_t MAX_QUEUE_SIZE;

      SLink<T> m_inQueue;
      std::unique_ptr<MailSlot<T>[]> m_slots;
      using Dispatcher = MailDispatcher<SLink<T>, MailSlot<T>>;
      Dispatcher m_dispatcher;
};


template <typename T>
ThreadPool<T>::ThreadPool(size_t queue_size, size_t pool_size)
   : MAX_QUEUE_SIZE((queue_size != 0) ? queue_size : DEFAULT_QUEUE_SIZE),
     m_slots(new MailSlot<T>[POOL_SIZE(pool_size)]),
     m_dispatcher(m_inQueue, m_slots.get(), POOL_SIZE(pool_size))
{
   m_dispatcher.start();
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
   m_dispatcher.stop();
}

template <typename T>
bool ThreadPool<T>::post(T t)
{
   if (m_inQueue.size() > MAX_QUEUE_SIZE) {
      return false;
   }

   m_inQueue.push(std::move(t));
   m_dispatcher.gotMail();

   return true;
}


}
#endif
