#ifndef HQW_SINGLETON_HPP
#define HQW_SINGLETON_HPP

namespace hqw
{
template <typename Derived>
class Singleton 
{
public:
    Singleton(const Singleton&) = delete;
    Singleton& operator = (const Singleton&) = delete;
    static Derived& getInstance();    
protected:
    Singleton() = default;
};

template <typename Derived>
Derived& Singleton<Derived>::getInstance()
{
    static Derived instance;
    return instance;
}
}
#endif

