#ifndef _HQW_TESTCASEMANAGER_HPP_
#define _HQW_TESTCASEMANAGER_HPP_
#include <functional>
#include <memory>
#include <map>
#include <string>
#include "Singleton.hpp"

namespace 
{
template <typename T>
struct Creator
{
    T* operator ()()
    {
        return new T();
    }
};

}

namespace hqw
{
class TestCase;

class TestCaseManager : public Singleton<TestCaseManager>
{
  typedef std::map<std::string, std::function<TestCase*()>> CaseMap;
public:
    template <typename T>
    bool registerCase(const char* name, T* = nullptr)
    {
        return getMap().insert(std::make_pair(name, Creator<T>())).second;
    }

    std::unique_ptr<TestCase> getTestCase(const char* name) const
    {
      CaseMap::const_iterator it = getMap().find(name);
      if (it != getMap().end())
      {
        return std::unique_ptr<TestCase>((it->second)());
      }
      return std::unique_ptr<TestCase>();
    }

    static CaseMap& getMap()
    {
        static CaseMap theMap;
        return theMap;
    }
private:
    friend class Singleton<TestCaseManager>;
    TestCaseManager() = default;
    TestCaseManager(const TestCaseManager&) = delete;
    TestCaseManager& operator = (const TestCaseManager&) = delete;
};

}

#endif
