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
      if (iNode * node = dynamic_cast<iNode*>(pNode))
      {
        ++node->data;
        iNode::nodes_type& children = node->getChildren();
        for_each(children.begin(), children.end(), 
            [this](unique_ptr<hqw::Node>& node){node->accept(this);});
      }
    }
    void visitLeafNode(hqw::Node* pNode)
    {
      if (lNode * node = dynamic_cast<lNode*>(pNode))
      {
        ++node->data;
      }
    }
};

class Decrease : public hqw::NodeVisitor
{
  public:
    void visitInnerNode(hqw::Node* pNode)
    {
      if (iNode * node = dynamic_cast<iNode*>(pNode))
      {
        --node->data;
        iNode::nodes_type& children = node->getChildren();
        for (iNode::nodes_type::iterator it = children.begin();
            it != children.end();
            ++it)
        {
          (*it)->accept(this);
        }
      }
    }
    void visitLeafNode(hqw::Node* pNode)
    {
      if (lNode * node = dynamic_cast<lNode*>(pNode))
      {
        --node->data;
      }
    }
};

unique_ptr<hqw::Node> createNodeTree()
{
  unique_ptr<iNode> root(new iNode());
  unique_ptr<iNode> node1(new iNode());
  unique_ptr<iNode> node2(new iNode());
  unique_ptr<lNode> node4(new lNode());
  node2->addChild(std::move(node4));
  unique_ptr<lNode> node3(new lNode());
  node1->addChild(std::move(node3));
  root->addChild(std::move(node1));
  root->addChild(std::move(node2));
  return std::move(root);
}

void checkData(hqw::Node* pNode, int val)
{
  if (iNode* node = dynamic_cast<iNode*>(pNode))
  {
    CPPUNIT_ASSERT(node->data == val);
    iNode::nodes_type& children = node->getChildren();
    for (iNode::nodes_type::iterator it = children.begin();
        it != children.end();
        ++it)
    {
      checkData((*it).get(), val);
    }
  }
  else if (lNode* node = dynamic_cast<lNode*>(pNode))
  {
    CPPUNIT_ASSERT(node->data == val);
  }
  else
  {
    CPPUNIT_ASSERT(false);
  }
}

void checkSameTree(hqw::Node* pNode, hqw::Node* pNode2, bool sameData = true)
{
  CPPUNIT_ASSERT(pNode != pNode2);
  if (iNode* node = dynamic_cast<iNode*>(pNode))
  {
    iNode* node2 = dynamic_cast<iNode*>(pNode2);
    CPPUNIT_ASSERT(node2 != NULL);
    CPPUNIT_ASSERT(node != node2);
    if (sameData)
    {
      CPPUNIT_ASSERT(node->data == node2->data);
    }
    else
    {
      CPPUNIT_ASSERT(node->data != node2->data);
    }
    iNode::nodes_type& children = node->getChildren();
    iNode::nodes_type& children2 = node2->getChildren();
    CPPUNIT_ASSERT(children.size() == children2.size());
    for (iNode::nodes_type::iterator it = children.begin(), it2 = children2.begin();
        it != children.end();
        ++it, ++it2)
    {
      checkSameTree((*it).get(), (*it2).get(), sameData);
    }
  }
  else if (lNode* node = dynamic_cast<lNode*>(pNode))
  {
    lNode* node2 = dynamic_cast<lNode*>(pNode2);
    CPPUNIT_ASSERT(node2 != NULL);
    CPPUNIT_ASSERT(node != node2);
    if (sameData)
    {
      CPPUNIT_ASSERT(node->data == node2->data);
    }
    else
    {
      CPPUNIT_ASSERT(node->data != node2->data);
    }
  }
  else
  {
    CPPUNIT_ASSERT(false);
  }
}

}
void TestNode::testClone()
{
  unique_ptr<hqw::Node> pTree(createNodeTree());
  checkData(pTree.get(), 0);
  unique_ptr<hqw::Node> pTree2(pTree->clone());
  checkSameTree(pTree.get(), pTree2.get());
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
