#ifndef TEST_NODE_HPP
#define TEST_NODE_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestThreeSumZero : public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestThreeSumZero);
    CPPUNIT_TEST(testSum);
    CPPUNIT_TEST_SUITE_END();
public:
    void testSum();
};

#endif
