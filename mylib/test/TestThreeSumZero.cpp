#include "TestThreeSumZero.hpp"
#include "ThreeSumZero.hpp"
#include <iterator>
#include <algorithm>
#include <sstream>

using namespace std;

struct prTriple
{
  prTriple(ostringstream& out) : m_out(out) {}
  template <typename Iter>
  void operator () (const boost::tuple<Iter, Iter, Iter>& r)
  {
    m_out << *boost::get<0>(r) << "," 
          << *boost::get<1>(r) << ","
          << *boost::get<2>(r) << endl;
  }

  string getString() const
  {
    return m_out.str();
  }
  private:
    ostringstream& m_out;
};

void TestThreeSumZero::testSum()
{
  int vec[] = {10, 2, 3 ,-10, -12, 9, 8, 23, -7, 0, 73, 28, 39, -32};
  std::sort(begin(vec), end(vec));
  auto res = hqw::ThreeSumZero(begin(vec), end(vec));

  ostringstream ss;
  prTriple prt(ss);
  for_each(begin(res), end(res), prt);
  string resStr = "-32,-7,39\n"
                  "-32,9,23\n"
                  "-32,23,9\n"
                  "-32,39,-7\n"
                  "-12,2,10\n"
                  "-12,3,9\n"
                  "-12,9,3\n"
                  "-12,10,2\n"
                  "-10,0,10\n"
                  "-10,2,8\n"
                  "-10,8,2\n"
                  "-10,10,0\n"
                  "-7,39,-32\n"
                  "0,10,-10\n"
                  "2,8,-10\n"
                  "2,10,-12\n"
                  "3,9,-12\n"
                  "9,23,-32\n";
  CPPUNIT_ASSERT(prt.getString() == resStr);
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestThreeSumZero);
