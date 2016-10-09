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
   long offset = 0;
   using offset_t = decltype(offset);

   vector<pair<deque<decltype(offset)>, int>> pos(distance(pBeg, pEnd));

   for (auto e = beg ; e != end ; ++e, ++offset) {
      auto i = 0;
      auto addedPos = 0;
      for (auto t = pBeg ; t != pEnd ; ++t, ++i) {
         if (*t == *e) {
            if (addedPos > 0) {
               // there is duplicated elem in pattern
               // put the index +1 of first same elem
               // in the offset queue and only once.
               if (pos[i].first.empty()) {
                  pos[i].first.push_back(-addedPos);
                  pos[addedPos-1].second++;
               }
            } else {
               pos[i].first.push_back(offset);
               if (pos[i].second == 0) {
                  pos[i].second = 1;
               }
               // +1 to avoid conflict with offset 0
               addedPos = i+1;
            }
         }
      }
   }
   // check if all elem in pat are found
   for (auto& e : pos) {
      if (e.first.empty()) return make_pair(beg, beg);
   }

   offset = 0;
   offset_t minRangeLen = numeric_limits<offset_t>::max();
   auto minRange = make_pair<offset_t, offset_t>(0, 0);
   do {
      offset_t minOffset = numeric_limits<offset_t>::max();
      offset_t maxOffset = numeric_limits<offset_t>::min();
      auto minElem = 0;
      for (int i = 0 ; i < pos.size() ; ++i) {
         auto o = pos[i].first.front();
         if (o < 0) {
            continue;
         }
         auto count = 0;
         do {
            if (o > maxOffset) maxOffset = o;
            if (o < minOffset) {
               minOffset = o;
               minElem = i;
            }
            ++count;
            if (count < pos[i].second) {
               o = pos[i].first[count];
            }
         } while (count < pos[i].second);
      }
      auto rangeLen = maxOffset - minOffset + 1;
      if (rangeLen < minRangeLen) {
         minRangeLen = rangeLen;
         minRange.first = minOffset;
         minRange.second = maxOffset;
         if (rangeLen == pos.size()) break;
      }
      pos[minElem].first.pop_front();
      if (pos[minElem].first.empty()) break;
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

