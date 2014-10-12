#ifndef TEST_HQW_TESTCASE_HPP
#define TEST_HQW_TESTCASE_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestHqwTestCase: public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestHqwTestCase);
    CPPUNIT_TEST(testRunTest);
    CPPUNIT_TEST_SUITE_END();
public:
    void testRunTest();
};


#endif
