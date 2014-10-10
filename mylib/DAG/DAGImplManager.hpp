#ifndef _HQW_DAG_IMPL_MANAGER_HPP_
#define _HQW_DAG_IMPL_MANAGER_HPP_

#include <boost/function.hpp>
#include <map>
#include "IDAG.h"

namespace hqw { namespace impl {

template <typename T>
struct DefaultDAGImplCreator
{
  IDAG * operator () ()
  {
    return new T();
  }
};

class DAGImplManager
{
  private:
    typedef boost::function<IDAG*()> creator_type;
    typedef std::map<std::string, creator_type> DAGImplCreatorMap; 
  public:
    static DAGImplManager& getInstance()
    {
      static DAGImplManager theInstance;
      return theInstance;
    }

    IDAG* createDAGImpl(const char * id)
    {
      DAGImplCreatorMap::iterator it = getMap().find(id);
      return (it != getMap().end()) ? it->second() : NULL;
    }

  private:
    friend class DAGImplRegister;

    template <typename T>
    bool registerDAGImpl(const char * id, T* p = NULL)
    {
      return getMap().insert(std::make_pair(id, creator_type(DefaultDAGImplCreator<T>()))).second;
    } 
    template <typename T, typename Creator>
    bool registerDAGImpl(const char * id, Creator& c, T* p = NULL)
    {
      return getMap().insert(std::make_pair(id, c)).second;
    } 

    DAGImplCreatorMap& getMap()
    {
      static DAGImplCreatorMap theMap;
      return theMap;
    }
    DAGImplManager() {}
    DAGImplManager(const DAGImplManager&);
    DAGImplManager& operator = (const DAGImplManager&);
};


}}

#endif

