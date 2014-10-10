#ifndef TEST_TRANSACTION_HPP
#define TEST_TRANSACTION_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestTransaction: public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestTransaction);
    CPPUNIT_TEST(testTransInt);
    CPPUNIT_TEST(testTransPointer);
    CPPUNIT_TEST(testTransObject);
    CPPUNIT_TEST_SUITE_END();
public:
    void testTransInt();
    void testTransPointer();
    void testTransObject();
};


#endif
