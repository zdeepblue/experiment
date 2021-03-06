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
      using Data = DataWrapper<T>;
      LeafNode& operator = (const LeafNode&) = delete;
    public:
      LeafNode() = default;
      explicit LeafNode(T data)
        : Data(std::move(data))
      {}
      const T& getData() const&
      {
        return Data::data;
      }
      T& getData() &
      {
        return Data::data;
      }
      T getData() &&
      {
        return std::move(Data::data);
      }
      void accept(NodeVisitor* pVisitor) override;
      LeafNode* clone() override;
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
