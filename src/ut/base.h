/**
 * @file base.h
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

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
