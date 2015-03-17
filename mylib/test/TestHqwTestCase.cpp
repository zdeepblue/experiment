#include "TestCase.hpp"
#include "TestHqwTestCase.hpp"
#include <string>

using namespace std;

class TestDemo1 : public hqw::TestCaseBase<TestDemo1>
{
  public:
    static const char* getName() { return "TestDemo1";}
    TestDemo1 () {}
    bool run()
    {
      ++s_runs;
      return true;
    }
    int getRuns()
    {
      return s_runs;
    }
  private:
    static int s_runs;
};

int TestDemo1::s_runs = 0;

void TestHqwTestCase::testRunTest()
{
  unique_ptr<hqw::TestCase> pTC = hqw::TestCaseManager::getInstance().getTestCase(TestDemo1::getName());
  CPPUNIT_ASSERT(pTC != nullptr);
  pTC->run();
  TestDemo1* pDemo1 = dynamic_cast<TestDemo1*>(pTC.get());
  CPPUNIT_ASSERT(pDemo1 != nullptr);
  CPPUNIT_ASSERT(pDemo1->getRuns() == 1);
  pDemo1->run();
  CPPUNIT_ASSERT(pDemo1->getRuns() == 2);
}


CPPUNIT_TEST_SUITE_REGISTRATION(TestHqwTestCase);

