#include <cxxtest/TestSuite.h>

#include "stdsneezy.h"

class template : public CxxTest::TestSuite
{

 public:
  void testAddition(){
    TS_ASSERT_EQUALS(1+1, 2);
  }
};
