#ifndef TEST_THREADPOOL_HPP
#define TEST_THREADPOOL_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestThreadPool: public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestThreadPool);
    CPPUNIT_TEST(testPool);
    CPPUNIT_TEST_SUITE_END();
public:
    void testPool();
};


#endif
