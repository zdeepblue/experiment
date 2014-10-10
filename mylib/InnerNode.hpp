#ifndef HQW_INNERNODE_HPP
#define HQW_INNERNODE_HPP

#include "Node.hpp"
#include "NodeVisitor.hpp"
#include "DataWrapper.hpp"

namespace hqw
{
  template <typename T>
  class InnerNode : public Node, public DataWrapper<T>
  {
    private:
      InnerNode& operator = (const InnerNode&);
    public:
      void accept(NodeVisitor* pVisitor);
      InnerNode * clone();
  };

  template <typename T>
  inline void InnerNode<T>::accept(NodeVisitor* pVisitor)
  {
    pVisitor->visitInnerNode(this);
  }

  template <typename T>
  inline InnerNode<T>* InnerNode<T>::clone()
  {
    return new InnerNode<T>(*this);
  }

}

#endif
