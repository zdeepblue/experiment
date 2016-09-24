#include <utility>
#include <iterator>
#include <deque>
#include <vector>
#include <limits>

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
   // check if all elem in pat are found
   for (auto e : pos) {
      if (e.empty()) return make_pair(beg, beg);
   }

   offset = 0;
   offset_t minRangeLen = numeric_limits<offset_t>::max();
   auto minRange = make_pair<offset_t, offset_t>(0, 0);
   do {
      offset_t minOffset = numeric_limits<offset_t>::max();
      offset_t maxOffset = numeric_limits<offset_t>::min();
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
int main(int argc, char * argv[])
{
   if (argc != 3) {
      cerr << "Usage: "  << argv[0] << " <string of text> <search pattern>" << endl;
      return -1;
   }
   string str(argv[1]);
   string pat(argv[2]);

   auto ret = searchMinRange(begin(str), end(str), begin(pat), end(pat));
   cout << "min range of '" << str << "' contains '" << pat
        <<  "' is '" << string(ret.first, ret.second)
        << "' at [" << distance(begin(str), ret.first) << ", " << distance(begin(str), ret.second) << ")"
        << endl;
}

