#ifndef HQW_SINGLETON_HPP
#define HQW_SINGLETON_HPP

#include <boost/noncopyable.hpp>

namespace hqw
{
template <typename Derived>
class Singleton : public boost::noncopyable
{
public:
    static Derived* getInstance();    

};

template <typename Derived>
Derived* Singleton<Derived>::getInstance()
{
    static Derived instance;
    return &instance;
}
}
#endif

