#include "TestSingleton.hpp"
#include "Singleton.hpp"
using namespace CppUnit;

class single : public hqw::Singleton<single>
{
    friend class hqw::Singleton<single>;
    single() {}
};

class single1 : public hqw::Singleton<single1>
{
    friend class hqw::Singleton<single1>;
    single1() {}
};


void TestSingleton::testGetInstance()
{
    single* pInst = single::getInstance();
    CPPUNIT_ASSERT(pInst != NULL);
    CPPUNIT_ASSERT(pInst == single::getInstance());
    single1* pInst1 = single1::getInstance();
    CPPUNIT_ASSERT(static_cast<void *>(pInst) != static_cast<void*>(pInst1));
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestSingleton);
