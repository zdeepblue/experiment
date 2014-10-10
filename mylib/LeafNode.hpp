#ifndef HQW_LEAFNODE_HPP
#define HQW_LEAFNODE_HPP

#include "Node.hpp"
#include "NodeVisitor.hpp"
#include "DataWrapper.hpp"

namespace hqw
{
  template <typename T>
  class LeafNode : public Node, public DataWrapper<T>
  {
    private:
      LeafNode& operator = (const LeafNode&);
    public:
      void accept(NodeVisitor* pVisitor);
      LeafNode* clone();
  };

  template <typename T>
  void LeafNode<T>::accept(NodeVisitor* pVisitor)
  {
    pVisitor->visitLeafNode(this);
  }  

  template <typename T>
  LeafNode<T>* LeafNode<T>::clone()  
  {
    return new LeafNode<T>(*this);
  }   
}

#endif
