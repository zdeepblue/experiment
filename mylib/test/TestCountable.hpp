#ifndef TEST_COUNTABLE_HPP
#define TEST_COUNTABLE_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestCountable: public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestCountable);
    CPPUNIT_TEST(testGetCount);
    CPPUNIT_TEST_SUITE_END();
public:
    void testGetCount();
};


#endif
