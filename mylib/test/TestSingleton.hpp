#ifndef TEST_SINGLETON_HPP
#define TEST_SINGLETON_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestSingleton : public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestSingleton);
    CPPUNIT_TEST(testGetInstance);
    CPPUNIT_TEST_SUITE_END();
public:
    void testGetInstance();
};


#endif
