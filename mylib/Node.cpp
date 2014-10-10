#include "Node.hpp"

namespace hqw
{
  Node::~Node()
  {
    for (nodes_type::iterator it = m_children.begin();
        it != m_children.end();
        ++it)
    {
      delete *it;
    }
  }

  Node::Node(const Node& rhs)
    : m_parent(rhs.m_parent)
  {
    for (nodes_type::const_iterator it = rhs.m_children.begin();
        it != rhs.m_children.end();
        ++it)
    {
      Node * child = (*it)->clone();
      child->m_parent = this;
      m_children.push_back(child);
    }
  }
}
