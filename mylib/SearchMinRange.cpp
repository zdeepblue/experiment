#include <utility>
#include <iterator>
#include <deque>
#include <vector>

template <typename Iter, typename Iter2>
std::pair<Iter, Iter> searchMinRange(Iter beg, Iter end,
                                     Iter2 pBeg, Iter2 pEnd)
{
   using namespace std;
   size_t offset = 0;
   using offset_t = decltype(offset);

   vector<deque<decltype(offset)>> pos(distance(pBeg, pEnd));

   for (auto e = beg ; e != end ; ++e, ++offset) {
      auto i = 0;
      for (auto t = pBeg ; t != pEnd ; ++t, ++i) {
         if (*t == *e) {
            pos[i].push_back(offset);
         }
      }
   }
   // TODO check if all elem in pat are found

   offset = 0;
   decltype(offset) minRangeLen = -1;
   auto minRange = make_pair<offset_t, offset_t>(0, 0);
   do {
      offset_t minOffset = -1;
      offset_t maxOffset = 0;
      auto minElem = 0;
      for (int i = 0 ; i < pos.size() ; ++i) {
         auto o = pos[i].front();
         if (o > maxOffset) maxOffset = o;
         if (o < minOffset) {
            minOffset = o;
            minElem = i;
         }
      }
      auto rangeLen = maxOffset - minOffset + 1;
      if (rangeLen < minRangeLen) {
         minRangeLen = rangeLen;
         minRange.first = minOffset;
         minRange.second = maxOffset;
         if (rangeLen == pos.size()) break;
      }
      pos[minElem].pop_front();
      if (pos[minElem].empty()) break;
   } while (true);
   return make_pair<Iter, Iter>(next(beg, minRange.first),
                                next(beg, minRange.second+1));
}

#include <iostream>
using namespace std;
int main()
{
   string str = "abcdssfrghsslg[ieneeb";
   string pat = "se";

   auto ret = searchMinRange(begin(str), end(str), begin(pat), end(pat));
   cout << "min range of '" << str << "' contains '" << pat
        <<  "' is '" << string(ret.first, ret.second)
        << "' at [" << distance(begin(str), ret.first) << ", " << distance(begin(str), ret.second) << ")"
        << endl;
}

