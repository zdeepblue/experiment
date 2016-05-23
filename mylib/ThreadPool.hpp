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

      using value_type = T;
      MailSlot()
         : m_quit(false),
           m_pvSlots(nullptr), m_cvSlots(nullptr),
           m_worker(std::bind(&MailSlot::workerLoop, this))
      {
      }

      ~MailSlot()
      {
         m_quit = true;
         m_cvMail.notify_one();
         m_worker.join();
      }

      void init(std::atomic<size_t> *pv, LockFreeCV *cv)
      {
        m_pvSlots = pv;
        m_cvSlots = cv;
      }

      bool empty() const
      {
         return std::atomic_load(&m_mail)==nullptr;
      }

      bool put(T t)
      {
         decltype(m_mail) nul;
         if (std::atomic_compare_exchange_weak(&m_mail, &nul, std::move(t))) {
            --(*m_pvSlots);
            m_cvMail.notify_one();
            return true;
         }
         return false;
      }
   private:
      void workerLoop();

      volatile std::atomic<bool> m_quit;
      std::atomic<size_t> *m_pvSlots;
      LockFreeLock m_lck;
      LockFreeCV m_cvMail;
      LockFreeCV *m_cvSlots;
      T m_mail;
      std::thread m_worker;
};

template <typename T>
void MailSlot<T>::workerLoop()
{
   while (true) {
      decltype(m_mail) p;
      bool quit = false;
      m_cvMail.wait(m_lck, [this, &p, &quit] () {
               return (p=std::atomic_load(&this->m_mail)) != nullptr ||
                      (quit=this->m_quit);
            });
      if (quit) {
         break;
      }
      try {
         (p->val)();
      } catch (...) {
      }
      ++(*m_pvSlots);
      while (!std::atomic_compare_exchange_weak(&m_mail, &p, T()))
      {}
      m_cvSlots->notify_one();
   }
}

template <typename Slot>
class MailSlots
{
   public:
      explicit MailSlots(size_t size)
         : m_pv(size), m_size(size), m_slots(new Slot[size])
      {
         for (size_t i = 0 ; i < m_size ; ++i) {
            m_slots[i].init(&m_pv, &m_cvSlots);
         }
         m_pred = [this] () { return this->m_pv > 0;};
      }

      void waitForEmptySlot(size_t ms = 0)
      {
         if (ms == 0) {
            m_cvSlots.wait(m_lck, m_pred);
         } else {
            m_cvSlots.wait_for(m_lck, std::chrono::milliseconds(ms), m_pred);
         }
         
      }
      bool select(const typename Slot::value_type& t);

   private:
      LockFreeLock m_lck;
      LockFreeCV m_cvSlots;
      std::atomic<size_t> m_pv;
      const size_t m_size;
      std::function<bool()> m_pred;
      std::unique_ptr<Slot[]> m_slots;
};

template <typename Slot>
bool MailSlots<Slot>::select(const typename Slot::value_type& t)
{
   size_t i = 0;
   for (; i < m_size ; ++i) {
      if (m_slots[i].empty() && m_slots[i].put(t)) {
         break;
      }
   }
   return (i != m_size);
}

template <typename Queue, typename Slots>
class MailDispatcher {
   public:
      MailDispatcher(Queue& q, Slots& slots)
         : m_queue(q), m_slots(slots), m_quit(false),
           m_disp(std::thread(std::bind(&MailDispatcher::dispatchLoop, this)))
      {
      }

      ~MailDispatcher()
      {
         m_quit = true;
         m_cvQueue.notify_one();
         m_disp.join();
      }

      void gotMail()
      {
         m_cvQueue.notify_one();
      }


   private:
      void dispatchLoop();

      Queue& m_queue;
      Slots& m_slots;
      volatile std::atomic<bool> m_quit;
      LockFreeLock m_lck;
      LockFreeCV m_cvQueue;
      std::thread m_disp;
};

template <typename Queue, typename Slots>
void MailDispatcher<Queue, Slots>::dispatchLoop()
{
   while (true) {
      bool quit = false;
      m_cvQueue.wait(m_lck, [this, &quit] () {return !this->m_queue.empty() || (quit=this->m_quit);} );
      if (quit) {
         break;
      }
      auto t = m_queue.pop();
      if (!t.hasValue()) {
         continue;
      }
      m_slots.waitForEmptySlot(1);
      while (!m_slots.select(t.getValue())) {
         m_slots.waitForEmptySlot(1);
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
      using Slots = MailSlots<MailSlot<typename SLink<T>::NodePtr>>;
      Slots m_slots;
      using Dispatcher = MailDispatcher<SLink<T>, Slots>;
      Dispatcher m_dispatcher;
};


template <typename T>
ThreadPool<T>::ThreadPool(size_t queue_size, size_t pool_size)
   : MAX_QUEUE_SIZE((queue_size != 0) ? queue_size : DEFAULT_QUEUE_SIZE),
     m_slots(POOL_SIZE(pool_size)),
     m_dispatcher(m_inQueue, m_slots)
{
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
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
