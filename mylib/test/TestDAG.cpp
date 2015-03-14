#include "TestDAG.hpp"
#include "DAG.h"

#define  COL_ID  0
#define  COL_NAME 1

void TestDAG::testAutoRegDAG()
{
  hqw::DAG dagDemo("DemoTable1");
  CPPUNIT_ASSERT(dagDemo.getRowCount() == 0);
  dagDemo.addRow();
  CPPUNIT_ASSERT(dagDemo.getRowCount() == 1);
  // 1. COL_ID
  dagDemo.setValue(0, COL_ID, 10);
  long id = 0;
  dagDemo.getValue(0, COL_ID,id);
  CPPUNIT_ASSERT(id == 10);

  // 2. COL_NAME
  dagDemo.setValue(0, COL_NAME, "Steven Huang");
  std::string name;
  dagDemo.getValue(0, COL_NAME,name);
  CPPUNIT_ASSERT(name == "Steven Huang");

  // 3, duplicate 
  hqw::DAG dupDag(dagDemo);
  // make sure origin dag is unchanged
  CPPUNIT_ASSERT(dagDemo.getRowCount() == 1);
  dagDemo.getValue(0, COL_ID,id);
  CPPUNIT_ASSERT(id == 10);
  dagDemo.getValue(0, COL_NAME,name);
  CPPUNIT_ASSERT(name == "Steven Huang");
  // check the duplication is same as origin one
  CPPUNIT_ASSERT(dupDag.getRowCount() == 1);
  long id2 = 0;
  dupDag.getValue(0, COL_ID,id2);
  CPPUNIT_ASSERT(id2 == 10);
  std::string name2;
  dupDag.getValue(0, COL_NAME,name2);
  CPPUNIT_ASSERT(name == "Steven Huang");
  // change one do not effact other
  dupDag.setValue(0, COL_NAME, "Huang Qiwei");
  dupDag.getValue(0, COL_NAME,name2);
  CPPUNIT_ASSERT(name2 == "Huang Qiwei");
  dagDemo.getValue(0, COL_NAME, name);
  CPPUNIT_ASSERT(name == "Steven Huang");

  // uncomment out following lines to test invalid type 
  // expect an error of compilation
  //int idx = 0;
  //dagDemo.getValue(0, COL_ID, id2);
}

CPPUNIT_TEST_SUITE_REGISTRATION(TestDAG);
