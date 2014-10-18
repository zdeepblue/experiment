#ifndef TEST_UNION_FIND_HPP
#define TEST_UNION_FIND_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestUnionFind : public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestUnionFind);
    CPPUNIT_TEST(testConnection);
    CPPUNIT_TEST_SUITE_END();
public:
    void testConnection();
};

#endif
