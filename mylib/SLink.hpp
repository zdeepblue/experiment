#ifndef HOW_SLINK_HPP
#define HOW_SLINK_HPP

#include <memory>
#include <atomic>
#include <utility>

namespace hqw {

template <typename T>
class SLink {

   struct Node;
   using std::shared_ptr<Node> = NodePtr;

   struct Node {
      explicit Node (T&& t) : val(std::forward<T>(t)) {}
      T val;
      NodePtr next;
   };

   std::atomic<NodePtr> m_head;
   std::atomic<size_t> m_length;

public:
   using value_type = T;

   class Reference {
         NodePtr node;
         explicit Reference(NodePtr n)
            : node(std::move(n)) {}
         friend class SLink;
      public:
         bool hasValue() { return node != nullptr; }
         T& operator () const { return *node.get(); }
         T* operator & () const { return node.get(); }
   };

   SLink()
      : m_length(0)
   {}

   void push(T&& t);
   void pushRef(Reference& t);

   Reference pop();

   bool empty() const
   {
      return head == nullptr;
   }

   size_t size() const
   {
      return m_length;
   }

};

template <typename T>
void SLink::pushRef(Reference& t)
{
   auto node(t.node);
   node->next = head.load();

   while (!head.atomic_compare_exchange_weak(node->next, node))
   {}
   ++m_length;
}

template <typename T>
void SLink::push(T&& t)
{
   auto node = std::make_shared<Node>(std::forward<T>(t));
   node->next = head.load();

   while (!head.atomic_compare_exchange_weak(node->next, node))
   {}
   ++m_length;
}

template <typename T>
SLink<T>::Reference SLink<T>::pop()
{
   auto node = head.load();

   if (node != nullptr) {
      while (!head.atomic_compare_exchange_weak(node, node->next))
      {}
      node->next.release();
      --m_length;
   }
   return Reference(std::move(node));
}

}

#endif
