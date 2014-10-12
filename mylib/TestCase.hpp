#ifndef _HQW_TESTCASE_HPP_
#define _HQW_TESTCASE_HPP_

#include "TestCaseManager.hpp"
#include "AutoRegister.hpp"

namespace hqw {

class TestCase
{
public:
	virtual ~TestCase() {}
	virtual bool run() = 0;
};

struct CaseRegister
{
  template <typename T>
  bool operator () (T*)
  {
    return TestCaseManager::getInstance().registerCase<T>(T::getName());
  }
};

template <typename T>
class TestCaseBase : public TestCase, private AutoRegister<T, CaseRegister>
{
public:
  ~TestCaseBase()
  {
  }
};


}

#endif
