#include "TestSLink.hpp"
#include "SLink.hpp"

#include <thread>
#include <atomic>
#include <vector>
#include <array>


using namespace std;
using namespace hqw;

void addToQueue(int from, int to, SLink<int>& queue)
{
   for (int i = from ; i < to ; ++i) {
      queue.push(i);
   }
}

#define NUM_PROD 8
#define DATA_PER_PROD 1000
#define NUM_CONS 5
void TestSLink::testProdCustQueue()
{
   SLink<int> queue;
   atomic<bool> quit(false);

   array<atomic<int>, DATA_PER_PROD * NUM_PROD> data;
   for (auto & i : data) {
      i = 0;
   }

   vector<thread> prods;
   vector<thread> cons;

   prods.reserve(NUM_PROD);
   cons.reserve(NUM_CONS);

   for (int i = 0 ; i < NUM_PROD/2 ; ++i) {
      prods.push_back(thread(bind(&addToQueue, i*DATA_PER_PROD, (i+1)*DATA_PER_PROD, ref(queue))));
   }

   for (int i = 0 ; i < NUM_CONS ; ++i) {
      cons.push_back(thread([&queue, &data, &quit] () {
               while (!queue.empty() || !quit) {
                  auto t = queue.pop();
                  if (t.hasValue()) {
                     int i = *t;
                     ++data[i];
                  }
               }
            }));
   }
   for (int i = NUM_PROD/2 ; i < NUM_PROD ; ++i) {
      prods.push_back(thread(bind(&addToQueue, i*DATA_PER_PROD, (i+1)*DATA_PER_PROD, ref(queue))));
   }

   for (auto & t : prods) {
      t.join();
   }

   quit = true;
   for (auto & t : cons) {
      t.join();
   }

   for (auto & i : data) {
      CPPUNIT_ASSERT(i == 1);
   }

   
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestSLink);
