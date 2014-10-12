#include "TestTransaction.hpp"
#include "Transaction.hpp"
#include <string>

namespace 
{
template <typename T>
void testAutoRollback(const T& a, const T& b)
{
   T temp = a;
   {
       hqw::Transaction<T> trans(temp);
       temp = b;
       CPPUNIT_ASSERT(temp == b);
   }
   CPPUNIT_ASSERT(temp == a);
}

template <typename T>
void testManualRollback(const T& a, const T& b)
{
   T temp = a;
   {
       hqw::Transaction<T> trans(temp);
       temp = b;
       CPPUNIT_ASSERT(temp == b);
       trans.rollback();
       CPPUNIT_ASSERT(temp == a);
   }
   CPPUNIT_ASSERT(temp == a);
}

template <typename T>
void testCommit(const T& a, const T& b)
{
   T temp = a;
   {
       hqw::Transaction<T> trans(temp);
       temp = b;
       CPPUNIT_ASSERT(temp == b);
       trans.commit();
   }
   CPPUNIT_ASSERT(temp == b);
}

template <typename T>
void testTrans(const T& a, const T& b)
{
    testAutoRollback(a, b);
    testManualRollback(a, b);
    testCommit(a, b);
}
}

void TestTransaction::testTransInt()
{
    testTrans<int>(1, 2);
}

void TestTransaction::testTransPointer()
{
   int * a, * b;
   testTrans<int *>(a, b);
}
void TestTransaction::testTransObject()
{
    std::string a("aa");
    std::string b("bb");
    testTrans<std::string>(a, b);
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestTransaction);
