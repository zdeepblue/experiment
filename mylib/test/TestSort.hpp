#ifndef TEST_SORT_HPP
#define TEST_SORT_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestSort : public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestSort);
    CPPUNIT_TEST(testMergeSort);
    CPPUNIT_TEST(testInsertSort);
    CPPUNIT_TEST_SUITE_END();
public:
    void testMergeSort();
    void testInsertSort();
};


#endif
