#include "TestSort.hpp"
#include "sort.hpp"

namespace {



template <typename Iter>
void assertSorted(Iter beg, Iter end)
{
  for (Iter i = beg + 1; i != end ; ++i)
  {
    CPPUNIT_ASSERT(!(*i < *(i-1)));
  }
}

}
void TestSort::testInsertSort()
{
  int a[] = {2, 5, 6, 10, 300, 3, 5};
  hqw::insertSort(std::begin(a), std::end(a));
  assertSorted(std::begin(a), std::end(a));
}

void TestSort::testMergeSort()
{
  int a[] = {2, 5, 6, 10, 300, 3, 5, 111, 33, 55, 6544, 2, 2342, 56, 323, 4, 21, 44, 690, 109, 8, 9, 0 ,2 ,38, 98, 19, 74, 9};
  hqw::mergeSort(std::begin(a), std::end(a));
  assertSorted(std::begin(a), std::end(a));
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestSort);
