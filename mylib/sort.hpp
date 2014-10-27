#ifndef HQW_SORT_HPP
#define HQW_SORT_HPP

#include <iterator>
#include <vector>

namespace hqw {
template <typename Iter>
void insertSort(Iter beg, Iter end);
}

namespace {

  template <typename Iter, typename Iter2>
  void _mergeSort(Iter beg, Iter end, Iter2 axBeg)
  {
    typename std::iterator_traits<Iter>::difference_type len = std::distance(beg, end), half=len/2;
    if (len < 5) 
    {
      hqw::insertSort(beg, end);
      return;
    }
    _mergeSort(axBeg, axBeg+half, beg);
    _mergeSort(axBeg+half, axBeg+len, beg+half);
    if (*(axBeg+half-1) < *(axBeg+half))
    {
      Iter j = beg;
      for (Iter2 i = axBeg; i != axBeg+len ;)
      {
        *j++ = *i++;
      }
      return;
    }
    Iter k = beg; Iter2 i = axBeg, j = axBeg + half;
    while ( k != (beg + len) )
    {
      if (i == (axBeg + half))
      {
        *k++ = *j++;
      }
      else if (j == (axBeg + len))
      {
        *k++ = *i++;
      }
      else if (*j < *i)
      {
        *k++ = *j++;
      }
      else
      {
        *k++ = *i++;
      }
    }
  }

}

namespace hqw {

template <typename Iter>
void insertSort(Iter beg, Iter end)
{
  for (Iter i = beg+1; i != end ; ++i)
  {
    for (Iter j = i ; j != beg ; --j)
    {
      if (*j < *(j-1)) std::swap(*j, *(j-1));
    }
  }
}

template <typename Iter>
void mergeSort(Iter beg, Iter end)
{
  std::vector<typename std::iterator_traits<Iter>::value_type> aux(beg, end);
  ::_mergeSort(beg, end, aux.begin());
}

}
#endif
