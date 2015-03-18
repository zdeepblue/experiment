#ifndef TEST_FACTORY_HPP
#define TEST_FACTORY_HPP

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class TestFactory: public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(TestFactory);
    CPPUNIT_TEST(testInt);
    CPPUNIT_TEST(testObject);
    CPPUNIT_TEST(testFuncPtr);
    CPPUNIT_TEST(testFunctor);
    CPPUNIT_TEST(testDeleter);
    CPPUNIT_TEST_SUITE_END();
public:
    void testInt();
    void testObject();
    void testFuncPtr();
    void testFunctor();
    void testDeleter();
};


#endif
