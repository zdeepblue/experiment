#ifndef HQW_SINGLETON_HPP
#define HQW_SINGLETON_HPP
namespace hqw
{
template <typename Derived>
class Singleton
{
protected:
    Singleton(){}
    Singleton(const Singleton&);
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

