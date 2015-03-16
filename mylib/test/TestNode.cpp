#include <algorithm>
#include "TestNode.hpp"
#include "InnerNode.hpp"
#include "LeafNode.hpp"

using namespace CppUnit;
using namespace std;

typedef hqw::InnerNode<int> iNode;
typedef hqw::LeafNode<int> lNode;

namespace
{
class Increase : public hqw::NodeVisitor
{
  public:
    void visitInnerNode(hqw::Node* pNode)
    {
      iNode * node = static_cast<iNode*>(pNode);
      ++node->getData();
      iNode::nodes_type& children = node->getChildren();
      for_each(children.begin(), children.end(), 
          [this](unique_ptr<hqw::Node>& node){node->accept(this);});
    }
    void visitLeafNode(hqw::Node* pNode)
    {
      lNode * node = static_cast<lNode*>(pNode);
      ++node->getData();
    }
};

class Decrease : public hqw::NodeVisitor
{
  public:
    void visitInnerNode(hqw::Node* pNode)
    {
      iNode * node = static_cast<iNode*>(pNode);
      --node->getData();
      iNode::nodes_type& children = node->getChildren();
      for_each(children.begin(), children.end(), 
          [this] (unique_ptr<hqw::Node>& node) { node->accept(this);} );
    }
    void visitLeafNode(hqw::Node* pNode)
    {
      lNode * node = static_cast<lNode*>(pNode);
      --node->getData();
    }
};

unique_ptr<hqw::Node> createNodeTree(int i = 0)
{
  unique_ptr<iNode> root(new iNode(i));
  unique_ptr<iNode> node1(new iNode(i));
  unique_ptr<iNode> node2(new iNode(i));
  unique_ptr<lNode> node4(new lNode(i));
  node2->addChild(std::move(node4));
  unique_ptr<lNode> node3(new lNode(i));
  node1->addChild(std::move(node3));
  root->addChild(std::move(node1));
  root->addChild(std::move(node2));
  return std::move(root);
}

void checkData(const hqw::Node* pNode, int val)
{
  if (const iNode* node = dynamic_cast<const iNode*>(pNode))
  {
    CPPUNIT_ASSERT(node->getData() == val);
    const iNode::nodes_type& children = node->getChildren();
    for_each(children.cbegin(), children.cend(), 
        [val](const unique_ptr<hqw::Node>& node) { checkData(node.get(), val);} );
  }
  else if (const lNode* node = dynamic_cast<const lNode*>(pNode))
  {
    CPPUNIT_ASSERT(node->getData() == val);
  }
  else
  {
    CPPUNIT_ASSERT(false);
  }
}

bool isEquivalentTree(const hqw::Node* pNode, const hqw::Node* pNode2)
{
  if (pNode == pNode2) return false;
  if (const iNode* node = dynamic_cast<const iNode*>(pNode))
  {
    const iNode* node2 = dynamic_cast<const iNode*>(pNode2);
    if (node2 == nullptr || node == node2 || node->getData() != node2->getData())
    {
      return false;
    }

    const iNode::nodes_type& children = node->getChildren();
    const iNode::nodes_type& children2 = node2->getChildren();
    if (children.size() != children2.size()) return false;
    for (iNode::nodes_type::const_iterator it = children.cbegin(), it2 = children2.cbegin();
        it != children.cend();
        ++it, ++it2)
    {
      if (!isEquivalentTree((*it).get(), (*it2).get()))
      {
        return false;
      }
    }
  }
  else if (const lNode* node = dynamic_cast<const lNode*>(pNode))
  {
    const lNode* node2 = dynamic_cast<const lNode*>(pNode2);
    if (node2 == nullptr || node == node2 || node->getData() != node2->getData())
    {
      return false;
    }
  }
  else
  {
    CPPUNIT_ASSERT(false);
  }
  return true;
}

}
void TestNode::testClone()
{
  unique_ptr<hqw::Node> pTree(createNodeTree(12));
  checkData(pTree.get(), 12);
  unique_ptr<hqw::Node> pTree2(pTree->clone());
  CPPUNIT_ASSERT(isEquivalentTree(pTree.get(), pTree2.get()));
  dynamic_cast<iNode*>(pTree2.get())->getData()++;
  CPPUNIT_ASSERT(!isEquivalentTree(pTree.get(), pTree2.get()));
}

void TestNode::testVisitor()
{
  unique_ptr<hqw::Node> pTree = createNodeTree();
  checkData(pTree.get(), 0);
  Increase inc;
  pTree->accept(&inc);
  checkData(pTree.get(), 1);
  Decrease dec;
  pTree->accept(&dec);
  checkData(pTree.get(), 0);
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestNode);
