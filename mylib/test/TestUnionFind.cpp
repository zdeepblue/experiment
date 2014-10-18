#include "union-find.hpp"
#include "TestUnionFind.hpp"

void TestUnionFind::testConnection()
{
  typedef hqw::UnionFind<10> UF;
  UF uf;
  uf.union2(2, 5);
  uf.union2(5, 9);
  uf.union2(1, 6);
  uf.union2(7, 3);
  uf.union2(6, 5);

  CPPUNIT_ASSERT(!uf.isConnected(4, 6));
  CPPUNIT_ASSERT(uf.isConnected(1, 9));
  CPPUNIT_ASSERT(!uf.isConnected(3, 2));
  
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestUnionFind);
