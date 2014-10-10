#include "TestCountable.hpp"
#include "Countable.hpp"
class Derive1 : public hqw::Countable<Derive1>
{};

class Derive2 : public hqw::Countable<Derive2>
{};

void TestCountable::testGetCount()
{
    Derive1 d1_1,d1_2;
    Derive2 d2_1;
    CPPUNIT_ASSERT(Derive1::getCount() == 2);
    CPPUNIT_ASSERT(Derive2::getCount() == 1);
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestCountable);
