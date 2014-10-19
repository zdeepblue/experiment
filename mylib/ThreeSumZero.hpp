#ifndef HQW_THREE_SUM_ZERO_HPP
#define HQW_THREE_SUM_ZERO_HPP

#include <boost/tuple/tuple.hpp>
#include <algorithm>


namespace hqw {

// [beg, end) must be sorted
template <typename Iter>
std::vector<boost::tuple<Iter, Iter, Iter> > ThreeSumZero(Iter beg, Iter end)
{
  std::vector<boost::tuple<Iter, Iter, Iter> > res;
  for (Iter i = beg ; i != end ; ++i)
  {
    for (Iter j = i+1; j != end ; ++j)
    {
      auto m2 = std::equal_range(beg, end, -(*i+*j));
      while (m2.first != m2.second)
      {
        res.push_back(boost::make_tuple(i, j, m2.first));
        ++m2.first;
      }
    }
  }
  return res;
}

}
#endif

