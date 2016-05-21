#ifndef TEST_SLINK_HPP
#define TEST_SLINK_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestSLink: public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestSLink);
    CPPUNIT_TEST(testProdCustQueue);
    CPPUNIT_TEST_SUITE_END();
public:
    void testProdCustQueue();
};


#endif
