#ifndef TEST_NODE_HPP
#define TEST_NODE_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestNode : public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestNode);
    CPPUNIT_TEST(testClone);
    CPPUNIT_TEST(testVisitor);
    CPPUNIT_TEST_SUITE_END();
public:
    void testClone();
    void testVisitor();
};

#endif
