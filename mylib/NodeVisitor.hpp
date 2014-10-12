#ifndef HQW_NODEVISITOR_HPP
#define HQW_NODEVISITOR_HPP

namespace hqw
{
  class NodeVisitor
  {
    public:
      virtual ~NodeVisitor() {}
      virtual void visitInnerNode(Node* node) = 0;
      virtual void visitLeafNode(Node* node) = 0;
  };
}
#endif
