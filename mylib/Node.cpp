#include <algorithm>
#include "Node.hpp"

using namespace std;

namespace hqw
{
  Node::Node(const Node& rhs)
    : m_parent(rhs.m_parent)
  {
    for_each(rhs.m_children.cbegin(), rhs.m_children.cend(), 
        [this](const unique_ptr<Node>& it){this->addChild(unique_ptr<Node>(it->clone()));});
  }
}
