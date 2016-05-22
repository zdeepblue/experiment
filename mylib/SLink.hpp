#ifndef HOW_SLINK_HPP
#define HOW_SLINK_HPP

#include <memory>
#include <atomic>
#include <utility>

namespace hqw {

template <typename T>
class SLink {

public:
   struct Node;
   using NodePtr = std::shared_ptr<Node>;

   struct Node {
      explicit Node (T&& t) : val(std::forward<T>(t)) {}
      T val;
      NodePtr next;
   };

   using value_type = T;

   class Reference {
         NodePtr node;
         explicit Reference(NodePtr n)
            : node(std::move(n)) {}
         friend class SLink;
      public:
         bool hasValue() { return node != nullptr; }
         NodePtr getValue() const { return node; }
         T& operator * () const { return node->val;}
         T* operator -> () const { return &node->val; }
   };

   SLink()
      : m_length(0)
   {}

   void push(T t);
   void push(Reference t);

   Reference pop();

   bool empty() const
   {
      return std::atomic_load(&m_head) == nullptr;
   }

   size_t size() const
   {
      return m_length;
   }

private:
   NodePtr m_head;
   std::atomic<size_t> m_length;

};

template <typename T>
void SLink<T>::push(Reference t)
{
   auto node(std::move(t.node));
   node->next = std::atomic_load(&m_head);

   while (!std::atomic_compare_exchange_weak(&m_head, &node->next, node))
   {}
   ++m_length;
}

template <typename T>
void SLink<T>::push(T t)
{
   auto node = std::make_shared<Node>(std::move(t));
   node->next = std::atomic_load(&m_head);

   while (!std::atomic_compare_exchange_weak(&m_head, &node->next, node))
   {}
   ++m_length;
}

template <typename T>
typename SLink<T>::Reference SLink<T>::pop()
{
   auto node = std::atomic_load(&m_head);

   while (node != nullptr &&
          !std::atomic_compare_exchange_weak(&m_head, &node, node->next))
   {}
   if (node != nullptr) --m_length;
   return Reference(std::move(node));
}

}

#endif
