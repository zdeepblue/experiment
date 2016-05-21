#include "TestThreadPool.hpp"
#include "ThreadPool.hpp"

#include <array>
#include <vector>

using namespace hqw;
using namespace std;

#define DATA_PER_PROD 100
#define NUM_PROD 10

void TestThreadPool::testPool()
{
   array<atomic<int>, DATA_PER_PROD * NUM_PROD> data; 
   for (auto & i : data) {
      i = 0;
   }

   auto f = [&data] (int id) { ++data[id]; };

   {
      ThreadPool<function<void ()>> pool;

      vector<thread> prods;
      prods.reserve(NUM_PROD);
      for (int i = 0 ; i < NUM_PROD ; ++i) {
         prods.push_back(thread( [&pool, &f, i] () {
                     for (int j = 0 ; j < DATA_PER_PROD ; ++j) {
                        auto f2 = bind(f, i*DATA_PER_PROD+j);
                        while (!pool.post(f2)) {}
                     }
                  } ));
      }

      for (auto &t : prods) {
         t.join();
      }
   }
   for (auto &i : data) {
      CPPUNIT_ASSERT(i == 1);
   }
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestThreadPool);
