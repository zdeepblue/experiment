#ifndef HQW_TEST_DEMO_TABLE_H
#define HQW_TEST_DEMO_TABLE_H

#include "DAGImpl.hpp"

DEFINE_DAGIMPL_BEGIN (DemoTable1)
  MAP_COLUMN_TYPE (0, long)
  MAP_COLUMN_TYPE (1, std::string)
DEFINE_DAGIMPL_END (DemoTable1, 2) 

#endif
