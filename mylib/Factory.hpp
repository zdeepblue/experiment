#ifndef HQW_FACTORY_HPP
#define HQW_FACTORY_HPP
#include <map>
#include <string>
#include <boost/function.hpp>
namespace
{
template <typename D>
struct Creator 
{
    D* operator () ()
    {
        return new D;
    }
};
}

namespace hqw
{
template <typename T, typename ID=std::string>
class Factory
{
public:    
    static T* createInstance(const ID&);

    /**
      * Default creator
      */
    template <typename D>
    static bool registerCreator(const ID&, const D* p=NULL);

    /**
      * creator is functor or function pointer
      */
    template <typename Functor>
    static bool registerCreator(const ID&, Functor);

private:
    typedef std::map<ID, boost::function<T*()> > creatorMap_t;
    static creatorMap_t& getCreatorMap()
    {
	static creatorMap_t creatorMap;
	return creatorMap;
    }
    
    template <typename D>
    static boost::function<T*()> makeCreator(const D* p=NULL)
    {
	Creator<D> d;
	boost::function<T*()> f = d;
	return f;
    }

    template <typename F>
    static boost::function<T*()> makeCreatorWrapper(F func)
    {
	boost::function<T*()> f = func;
	return f;
    }
};

template <typename T, typename ID>
T* Factory<T, ID>::createInstance(const ID& id)
{
    typename creatorMap_t::iterator it = getCreatorMap().find(id);
    if (it != getCreatorMap().end())
    {
	return (it->second)();
    }
    return NULL;
}

template <typename T, typename ID>
template <typename D>
bool Factory<T, ID>::registerCreator(const ID& id, const D*)
{
    return getCreatorMap().insert(make_pair(id, makeCreator<D>())).second;
}

template <typename T, typename ID>
template <typename F>
bool Factory<T, ID>::registerCreator(const ID& id, F func)
{
    return getCreatorMap().insert(make_pair(id, makeCreatorWrapper(func))).second;
}
}

#endif

