#ifndef HOW_SLINK_HPP
#define HOW_SLINK_HPP

#include <memory>
#include <atomic>

namespace hqw {

template <typename T>
class SLink {

   struct Node;
   using std::shared_ptr<Node> = NodePtr;

   struct Node {
      explicit Node (const T& t) : val(t) {}
      T val;
      NodePtr next;
   };

   std::atomic<NodePtr> m_head;

public:
   SLink()
   {}

   void push(const T& t);

   T pop();

   bool empty() const
   {
      return head == nullptr;
   }

   size_t size() const;

};


template <typename T>
void SLink::push(const T& t)
{
   auto node = std::make_shared<Node>(t);
   node->next = head.load();

   while (!head.atomic_compare_exchange_weak(node->next, node))
   {}
}

template <typename T>
T pop()
{
   auto node = head.load();
   auto t = std::move(node->val);

   while (!head.atomic_compare_exchange_weak(node, node->next))
   {}

   return t;
}


template <typename T>
size_t size() const
{
   size_t len = 0;
   auto node = head.load();
   while (node != nullptr) {
      ++len;
   }
   return len;
}

}

#endif
