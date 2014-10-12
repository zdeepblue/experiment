#ifndef HQW_COUNTABLE_HPP
#define HQW_COUNTABLE_HPP
namespace hqw 
{
template <typename Derived>
class Countable
{
    static long mCount;
public:
    static long getCount()
    {
	return mCount;
    }
    Countable()
    {
	++mCount;
    }
    ~Countable()
    {
	--mCount;
    }
};

template <typename Derived>
long Countable<Derived>::mCount = 0;
}
#endif
