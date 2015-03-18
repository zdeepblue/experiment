#ifndef HQW_THREE_SUM_ZERO_HPP
#define HQW_THREE_SUM_ZERO_HPP

#include <algorithm>
#include <iterator>


namespace hqw {

// [beg, end) must be sorted
template <typename Iter>
std::vector<std::tuple<Iter, Iter, Iter> > ThreeSumZero(Iter beg, Iter end)
{
  std::vector<std::tuple<Iter, Iter, Iter>> res;
  for (Iter i = beg ; i != end ; std::advance(i, 1))
  {
    for (Iter j = std::next(i); j != end ; std::advance(j,1))
    {
      auto m2 = std::equal_range(beg, end, -(*i+*j));
      while (m2.first != m2.second)
      {
        res.emplace_back(std::make_tuple(i, j, m2.first));
        std::advance(m2.first,1);
      }
    }
  }
  return res;
}

}
#endif

