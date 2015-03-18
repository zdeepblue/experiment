#ifndef HQW_FACTORY_HPP
#define HQW_FACTORY_HPP
#include <map>
#include <string>
#include <functional>
#include <memory>
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
    static std::unique_ptr<T> createInstance(const ID&);

    template <typename Del>
    static std::unique_ptr<T, Del> createInstance(const ID&, Del&&);

    /**
      * Default creator
      */
    template <typename D>
    static bool registerCreator(const ID&);

    /**
      * creator is functor or function pointer
      */
    template <typename Functor>
    static bool registerCreator(const ID&, Functor&&);
    
private:
    using Creator_t = std::function<T*()>;
    using creatorMap_t = std::map<ID, Creator_t>;

    static creatorMap_t& getCreatorMap()
    {
        static creatorMap_t creatorMap;
        return creatorMap;
    }

    template <typename D>
    static Creator_t makeCreator(const D* p=nullptr)
    {
        Creator<D> d;
        Creator_t f = d;
        return f;
    }

    template <typename F>
    static Creator_t makeCreatorWrapper(F&& func)
    {
        Creator_t f = std::forward<F>(func);
        return f;
    }
};

template <typename T, typename ID>
std::unique_ptr<T> Factory<T, ID>::createInstance(const ID& id)
{
    auto it = getCreatorMap().find(id);
    if (it != getCreatorMap().cend())
    {
        return std::unique_ptr<T>((it->second)());
    }
    return std::unique_ptr<T>();
}

template <typename T, typename ID>
template <typename Del>
std::unique_ptr<T, Del> Factory<T, ID>::createInstance(const ID& id, Del&& d)
{
    auto it = getCreatorMap().find(id);
    if (it != getCreatorMap().cend())
    {
        return std::unique_ptr<T, Del>((it->second)(), std::forward<Del>(d));
    }
    return std::unique_ptr<T,Del>(nullptr, std::forward<Del>(d));
}

template <typename T, typename ID>
template <typename D>
bool Factory<T, ID>::registerCreator(const ID& id)
{
    return getCreatorMap().insert(std::make_pair(id, makeCreator<D>())).second;
}

template <typename T, typename ID>
template <typename F>
bool Factory<T, ID>::registerCreator(const ID& id, F&& func)
{
    return getCreatorMap().insert(std::make_pair(id, makeCreatorWrapper(std::forward<F>(func)))).second;
}

}

#endif

