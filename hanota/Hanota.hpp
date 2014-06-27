#ifndef HANOTA_HPP
#define HANOTA_HPP
#include <iterator>

template <typename Move, typename Iter, typename Pilar>
void hanota(Iter begin, Iter end, 
            const Pilar& from, const Pilar& to, const Pilar& via, 
            Move mv)
{
  if (begin != end)
  {
    Iter temp = begin;
    std::advance(temp, std::distance(begin, end) - 1);
    if (temp == begin)
    {
        mv(begin, from, to);
    }
    else
    {
      hanota(begin, temp, from, via, to, mv);
      mv(temp, from, to);
      hanota(begin, temp, via, to, from, mv);
    }
  }
}

#endif
