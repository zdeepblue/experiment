#ifndef _HQW_TESTCASEMANAGER_HPP_
#define _HQW_TESTCASEMANAGER_HPP_
#include <map>
#include <string>

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

class TestCaseManager
{
  typedef std::map<std::string, TestCase*> CaseMap;
public:
	template <typename T>
	bool registerCase(const char* name, T* p = 0)
	{
		if (!p) p = Creator<T>()();
		return getMap().insert(std::make_pair(name, p)).second;
	}

        TestCase* getTestCase(const char* name) const
        {
          CaseMap::const_iterator it = getMap().find(name);
          if (it != getMap().end())
          {
            return it->second;
          }
          return 0;
        }

	static TestCaseManager& getInstance()
	{
		static TestCaseManager theTCMgr;
		return theTCMgr;
	}

	static CaseMap& getMap()
	{
		static CaseMap theMap;
		return theMap;
	}
private:
	TestCaseManager() {}
	TestCaseManager(const TestCaseManager&);
	TestCaseManager& operator = (const TestCaseManager&);
};

}

#endif
