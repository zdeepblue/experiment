#ifndef HQW_NODE_HPP
#define HQW_NODE_HPP

#include <list>
#include <memory>

namespace hqw
{
  class NodeVisitor;
  class Node
  {
    public:
      using nodes_type = std::list<std::unique_ptr<Node>>;
    private:
      nodes_type m_children;
      Node* m_parent;
      Node& operator = (const Node& rhs) = delete;
    protected:
      Node() 
        : m_parent(nullptr)
      {}
      // deep copy construct only
      Node(const Node& rhs);
      // move construct only
      Node(Node&& rhs) noexcept
        : m_children(std::move(rhs.m_children)), m_parent(rhs.m_parent)
      {
      }
    public:
      virtual ~Node() = default;
      nodes_type getChildren() &&
      {
        return std::move(m_children);
      }
      nodes_type& getChildren() &
      {
        return m_children;
      }
      const nodes_type& getChildren() const&
      {
        return m_children;
      }
      void addChild(std::unique_ptr<Node> child)
      {
        child->m_parent = this;
        m_children.emplace_back(std::move(child));
      }
      virtual Node* clone() = 0;
      virtual void accept(NodeVisitor* pVisitor) = 0;
  };
}

#endif

