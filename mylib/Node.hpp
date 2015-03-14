#ifndef HQW_NODE_HPP
#define HQW_NODE_HPP

#include <list>

namespace hqw
{
  class NodeVisitor;
  class Node;
  class Node
  {
    public:
      typedef std::list<Node*> nodes_type;
    private:
      nodes_type m_children;
      Node* m_parent;
      Node& operator = (const Node& rhs);
    protected:
      Node() 
        : m_parent(nullptr)
      {}
      // deep clean
      virtual ~Node();
      // deep copy
      Node(const Node& rhs);
    public:
      nodes_type& getChildren()
      {
        return m_children;
      }
      const nodes_type& getChildren() const
      {
        return m_children;
      }
      void addChild(Node* child)
      {
        m_children.push_back(child);
        child->m_parent = this;
      }
      virtual Node * clone() = 0;
      virtual void accept(NodeVisitor* pVisitor) = 0;
  };
}

#endif

