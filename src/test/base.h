#ifndef BASE_H__
#define BASE_H__

#include <gtest/gtest.h>

class Base : public ::testing::Test
{
protected:
  virtual void SetUp();
  virtual void TearDown();
};

#endif
