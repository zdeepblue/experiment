#include "TestFactory.hpp"
#include "Factory.hpp"
using namespace hqw;

bool registerInt = Factory<int>::registerCreator<int>("int");

void TestFactory::testInt()
{
    using std::unique_ptr;
    unique_ptr<int> pInt = Factory<int>::createInstance("int");
    CPPUNIT_ASSERT(pInt != nullptr);
    *pInt = 3;
    CPPUNIT_ASSERT(*pInt == 3);
}

struct Base
{
    virtual ~Base() {}
    virtual int getID() = 0;
};

struct Derived1 : public Base
{
    int getID()
    {
	return 1;
    }
};

struct Derived2 : public Base
{
    int getID()
    {
	return 2;
    }
};

bool registD1 = Factory<Base>::registerCreator<Derived1>("Derived1");
bool registD2 = Factory<Base>::registerCreator<Derived2>("Derived2");
void TestFactory::testObject()
{
    using std::unique_ptr;
    std::unique_ptr<Base> pB1 = Factory<Base>::createInstance("Derived1");
    CPPUNIT_ASSERT(pB1->getID() == 1);
    unique_ptr<Base> pB2 = Factory<Base>::createInstance("Derived2");
    CPPUNIT_ASSERT(pB2->getID() == 2);
}

struct Derived3 : public Base
{
    int getID()
    {
	return 3;
    }
    static Base* createD3()
    {
	return new Derived3;
    }
};

bool registD3 = Factory<Base>::registerCreator("Derived3", &Derived3::createD3);
void TestFactory::testFuncPtr()
{
    using std::unique_ptr;
    unique_ptr<Base> pB3 = Factory<Base>::createInstance("Derived3");
    CPPUNIT_ASSERT(pB3->getID() == 3);
}

struct Derived4 : public Base
{
    int getID() {return 4;}
};

bool registD4 = Factory<Base>::registerCreator("Derived4", []() { return new Derived4();} );
void TestFactory::testFunctor()
{
    std::unique_ptr<Base> pB4 = Factory<Base>::createInstance("Derived4");
    CPPUNIT_ASSERT(pB4->getID() == 4);
}

void TestFactory::testDeleter()
{
  bool del_called = false;
  auto del = [&del_called] (Base* p) mutable { del_called = true; delete p; };
  {
    std::unique_ptr<Base, decltype(del)> pB4 = Factory<Base>::createInstance("Derived4", del);
    CPPUNIT_ASSERT(pB4->getID() == 4);
  }
  CPPUNIT_ASSERT(del_called);

}

CPPUNIT_TEST_SUITE_REGISTRATION(TestFactory);

