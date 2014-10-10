#ifndef TEST_DAG_HPP
#define TEST_DAG_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestDAG: public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestDAG);
    CPPUNIT_TEST(testAutoRegDAG);
    CPPUNIT_TEST_SUITE_END();
public:
    void testAutoRegDAG();
};


#endif
