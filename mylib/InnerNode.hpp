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
      using Data = DataWrapper<T>;
      InnerNode& operator = (const InnerNode&) = delete;
    public:
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
      InnerNode * clone() override;
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
