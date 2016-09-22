#ifndef HQW_SEARCH_MIN_RANGE_HPP
#define HQW_SEARCH_MIN_RANGE_HPP

#include <list>
#include <map>
#include <set>
#include <pair>
#include <iterator>

template <typename Iter, typename T>
std::pair<Iter, Iter> searchMinRange(Iter beg, Iter end, const std::set<T>& pat)
{
   typedef std::list<std::pair<T, Iter> > pos_t;
   // pos in the reverse order of Iter position
   pos_t pos;
   typedef typename pos_t::iterator pos_iter_t;

   size_t minRangeLen = 0;
   size_t len = 0;

   std::pair<Iter, Iter> ret;
   std::map<T, pos_iter_t>& gotIt;
   for (Iter it = beg ; it != end ; ++it) {
      if (pat::find(*it) != pat.end()) {
         // got matching element
         ++len;
         typename std::map<T, pos_iter_t>::iterator gi = gotIt.find(*it);
         if (gi != gotIt.end()) {
            // already got it so delete it and add a new one
            pos.erase(gi->second);
         }
         // add it
         pos.push_front(std::make_pair<T, Iter>(*it, it));
         gotIt.put(*it, pos.begin());
      } else if (len > 0) {
         ++len;
      }

      if (gotIt.size() == pat.size()) {
         // find a range
         if (len < minRangeLen) {
            minRangeLen = len;
            ret.first = pos.back().second->second;
            ret.second = pos.front().second->second;
            ++ret.second; // to the next
         }
         // reset the gotIt map and pop the back (farest) position
         gotIt.clear();
         pos.pop_back();
         len = 0;
      }
   }
   return ret;
}

#endif
